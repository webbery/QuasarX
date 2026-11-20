/**
 * 五个专业 Agent 定义
 *
 * 渐进式披露：
 * - 每个 Agent 常驻的只有 `search`（能力检索/启用）+ 白名单里的 `ask_*`（跨 Agent 转交）
 * - 其余工具由 LLM 先 search 再 enable，**下一轮**才真正绑定进 tools 数组
 * - 绑定是每轮重建的（rawLlm.bindTools(currentTools)），enable 立刻在下一轮生效
 *
 * 为什么每轮重绑而不是一次性 bindTools：
 *   一次性绑定要么把所有工具都塞进 prompt（21 个工具 ≈ 6500 tok/轮），
 *   要么得改 state 存「本轮启用了什么」。后者要动 GraphState 和 checkpoint，
 *   而一次 search 只要 ~200 tok —— 单轮内用闭包变量累积更划算。
 */

import { AIMessage, HumanMessage, SystemMessage, ToolMessage } from "@langchain/core/messages";
import { StructuredToolInterface } from "@langchain/core/tools";
import { createLLM, getAgentConfig, AgentConfig } from ".";

import { AgentType, AGENT_SYSTEM_PROMPTS, AGENT_TOOL_CONFIG, GraphStateType, AgentEvent, UsageReporter } from "./types";
import { INTERNAL_TOOL_AGENT_MAP } from "./internalTools";
import { isToolArtifactResult, type ArtifactEnvelope } from "./artifacts";
import { getToolCatalog, type ToolCatalog } from "./toolCatalog";
import { createSearchTool } from "./searchTool";
import { uiTool } from "../tools/ui";

/** 从 LangChain 响应中提取 token 用量（无用量信息时返回 undefined） */
function extractUsage(response: any): { promptTokens: number; completionTokens: number; totalTokens: number } | undefined {
  const u = response?.usage_metadata;
  if (!u) return undefined;
  const promptTokens = u.input_tokens ?? 0;
  const completionTokens = u.output_tokens ?? 0;
  const totalTokens = u.total_tokens ?? promptTokens + completionTokens;
  if (!totalTokens) return undefined;
  return { promptTokens, completionTokens, totalTokens };
}

/**
 * 常驻工具：白名单里的内部转交工具（ask_xxx）
 *
 * search 工具不在这里 —— 它由 runAgentLoop 现造，因为需要闭包捕获「本次 run 启用了什么」。
 */
export function getResidentToolsForAgent(
  agentType: AgentType,
  internalTools: StructuredToolInterface[] = [],
): StructuredToolInterface[] {
  const names = AGENT_TOOL_CONFIG[agentType] ?? [];
  return internalTools.filter((t) => names.includes(t.name));
}

/**
 * 创建裸 LLM 实例（**不**绑定 Tool）
 *
 * 绑定放到每一轮循环里做，才能让「刚 enable 的工具」在下一轮出现在 tools 数组。
 */
function createRawLLM(config?: AgentConfig) {
  const resolved = config ?? getAgentConfig();
  if (!resolved) {
    throw new Error("LLM 配置未设置，请先在设置中配置 Agent");
  }
  return createLLM(resolved);
}

/**
 * Agent 执行结果
 *
 * - content：Agent 给出的最终回复文本
 * - delegation：若本轮调用了内部 Tool（ask_xxx），携带含 tool_calls 的 AIMessage，
 *   由上层交回 Supervisor 路由到目标 Agent
 */
interface AgentRunResult {
  content: string;
  delegation?: AIMessage;
}

/**
 * 把「模型调用了一个当前没绑定的工具」从 AIMessage 里剔除
 *
 * 为什么必须剔除：Anthropic 会校验历史消息里 tool_use 的 name 必须在 tools 数组中，
 * 否则下一次请求直接 400。OpenAI 虽然宽松，但留着也只会让模型误以为调用成功。
 * 对应地，这类调用也不再补 ToolMessage（否则 tool_result 找不到对应的 tool_use）。
 */
function sanitizeAiMessage(response: any, keepCalls: any[]): AIMessage {
  const allCalls: any[] = response.tool_calls ?? [];
  if (keepCalls.length === allCalls.length) return response as AIMessage;

  const keepNames = new Set(keepCalls.map((c) => c.name));
  const placeholder = "（部分工具调用未被识别，已忽略）";
  let content = response.content;
  if (Array.isArray(content)) {
    const filtered = content.filter(
      (block: any) => !(block?.type === "tool_use" && !keepNames.has(block.name)),
    );
    content = filtered.length ? filtered : placeholder;
  } else if (typeof content === "string" && content.trim() === "") {
    // 只带 tool_use 没有正文的响应，剔空后会让 Anthropic 报「content 不能为空」
    content = placeholder;
  }

  return new AIMessage({
    content,
    additional_kwargs: response.additional_kwargs ?? {},
    tool_calls: keepCalls,
    response_metadata: response.response_metadata,
    usage_metadata: response.usage_metadata,
    id: response.id,
  }) as unknown as AIMessage;
}

/**
 * Agent 执行函数：LLM → Tool Calling 循环 → 返回最终消息
 */
async function runAgentLoop(
  agentType: AgentType,
  messages: Array<SystemMessage | HumanMessage | AIMessage | ToolMessage>,
  residentTools: StructuredToolInterface[],
  catalog: ToolCatalog | null,
  allowedTools: string[],
  onEvent?: (event: AgentEvent) => void,
  onUsage?: UsageReporter,
  signal?: AbortSignal,
  maxToolCalls = 30,
  config?: AgentConfig,
): Promise<AgentRunResult> {
  let rawLlm: any;
  try {
    rawLlm = createRawLLM(config);
  } catch (err) {
    console.error(`[LangGraph] ${agentType} 创建 LLM 失败:`, err);
    onEvent?.({
      agent: agentType,
      content: `LLM 初始化失败: ${err instanceof Error ? err.message : String(err)}`,
      eventType: "error",
      timestamp: Date.now(),
    });
    return { content: "AI 服务不可用，请检查 LLM 配置。" };
  }

  const allowedSet = new Set(allowedTools);

  // 本次 run 内按需启用的工具。刻意不进 GraphState：
  // 下一轮对话重新检索一次（~200 tok）比每轮背着全部 schema 划算得多。
  const currentTools: StructuredToolInterface[] = [];
  const enableTool = (name: string): boolean => {
    if (!catalog) return false;
    const entry = catalog.get(name);
    if (!entry) return false;
    if (!currentTools.some((t) => t.name === name)) {
      currentTools.push(entry.instance);
    }
    return true;
  };

  // 常驻：search + ui + 白名单里的 ask_*
  if (catalog) {
    currentTools.push(
      createSearchTool({
        catalog,
        allowedTools,
        residentToolNames: residentTools.map((t) => t.name),
        onEnable: enableTool,
      }),
    );
  }
  // ui 是跨领域的展示入口（出图 / 进度 / 交互），每个 Agent 都要能展示结果，故常驻
  currentTools.push(uiTool, ...residentTools);

  let currentMessages = [...messages];
  let toolCallCount = 0;

  while (toolCallCount < maxToolCalls) {
    // 已被用户中止：抛出而非返回空串，否则会话层会误显示「Agent 未返回回复」
    if (signal?.aborted) {
      throw new DOMException("请求已取消", "AbortError");
    }

    // ★ 每轮重新绑定：上一轮 enable 的工具从这一轮起进入 tools 数组
    const llm = rawLlm.bindTools(currentTools);

    let response: any;
    try {
      response = await llm.invoke(currentMessages, { signal });
    } catch (err) {
      console.error(`[LangGraph] ${agentType} LLM 调用失败:`, err);
      onEvent?.({
        agent: agentType,
        content: `LLM 调用失败: ${err instanceof Error ? err.message : String(err)}`,
        eventType: "error",
        timestamp: Date.now(),
      });
      // LLM 调用失败是致命错误：不能吞成一句通用文本，否则会话层无法展示错误和重试入口
      throw err;
    }

    // 上报本次 LLM 调用的 token 用量
    const usage = extractUsage(response);
    if (usage) onUsage?.(usage);

    // 收集 thinking 内容
    // Anthropic 格式: thinking 在 content 数组中
    // OpenAI 格式: thinking 在 additional_kwargs 中
    if (Array.isArray(response.content)) {
      for (const block of response.content) {
        if (block.type === 'thinking' && block.thinking) {
          onEvent?.({
            agent: agentType,
            content: block.thinking,
            eventType: "thought",
            timestamp: Date.now(),
          });
          break;
        }
      }
    } else if (response.additional_kwargs?.thinking) {
      onEvent?.({
        agent: agentType,
        content: response.additional_kwargs.thinking as string,
        eventType: "thought",
        timestamp: Date.now(),
      });
    }

    // 检查是否有 Tool 调用
    const toolCalls: any[] = response.tool_calls ?? [];
    if (toolCalls.length === 0) {
      // 无 Tool 调用，返回最终回复
      const content = typeof response.content === "string" ? response.content : response.content.map((c: any) => c._getType?.() === "text" ? c.text : "").join("");
      onEvent?.({
        agent: agentType,
        content,
        eventType: "response",
        timestamp: Date.now(),
      });
      return { content };
    }

    // 执行 Tool 调用
    toolCallCount += toolCalls.length;

    // 先把「允许自动绑定」的调用判出来，再决定哪些调用要写回消息历史
    const executable = new Map<any, StructuredToolInterface>();
    for (const tc of toolCalls) {
      const existing = currentTools.find((t) => t.name === tc.name);
      if (existing) {
        executable.set(tc, existing);
        continue;
      }
      // 兜底：模型可能没 enable 就直接调（它从检索结果里看到了名字）。
      // 在白名单里就顺手绑上，别让一次可自动修复的失误变成一条错误消息。
      if (allowedSet.has(tc.name) && enableTool(tc.name)) {
        const bound = currentTools.find((t) => t.name === tc.name)!;
        executable.set(tc, bound);
        onEvent?.({
          agent: agentType,
          content: `自动启用工具: ${tc.name}（模型跳过了 enable 步骤）`,
          eventType: "thought",
          timestamp: Date.now(),
        });
        continue;
      }
      onEvent?.({
        agent: agentType,
        content: `忽略未启用的工具调用: ${tc.name}（请先用 search 检索并 enable）`,
        eventType: "error",
        timestamp: Date.now(),
      });
    }

    const keepCalls = toolCalls.filter((tc) => executable.has(tc));
    const aiMessage = sanitizeAiMessage(response, keepCalls);

    // AIMessage 必须先于 ToolMessage 进历史，否则 Anthropic 会因 tool_result 找不到 tool_use 而拒
    currentMessages.push(aiMessage);

    // 本轮是否触发了内部 Tool（ask_xxx）——触发则交回控制权，由 Supervisor 路由
    let internalTarget: string | undefined;

    for (const tc of keepCalls) {
      onEvent?.({
        agent: agentType,
        content: `调用工具: ${tc.name}(${JSON.stringify(tc.args)})`,
        eventType: "tool_call",
        timestamp: Date.now(),
      });

      const tool = executable.get(tc)!;
      if (INTERNAL_TOOL_AGENT_MAP[tc.name]) {
        // 内部 Tool：目标 Agent 由 Supervisor 决定，本 Agent 不再继续自问自答
        internalTarget = INTERNAL_TOOL_AGENT_MAP[tc.name];
      }

      try {
        const rawResult = await tool.invoke(tc.args);

        // === 双通道 ===
        // 工具可返回 { text, artifact }：text 回灌给 LLM，artifact 走事件通道进 UI。
        // LLM 完全不接触产物结构，避免它编造图表数据。
        let resultStr: string;
        let artifact: ArtifactEnvelope | undefined;

        if (rawResult instanceof ToolMessage) {
          resultStr = typeof rawResult.content === "string"
            ? rawResult.content
            : JSON.stringify(rawResult.content);
          artifact = (rawResult as unknown as { artifact?: ArtifactEnvelope }).artifact;
        } else if (isToolArtifactResult(rawResult)) {
          resultStr = rawResult.text;
          artifact = rawResult.artifact;
        } else {
          resultStr = typeof rawResult === "string" ? rawResult : JSON.stringify(rawResult);
        }

        onEvent?.({
          agent: agentType,
          content: `工具 ${tc.name} 返回: ${resultStr.substring(0, 500)}`,
          eventType: "tool_result",
          timestamp: Date.now(),
        });

        if (artifact) {
          onEvent?.({
            agent: agentType,
            content: `生成产物: ${artifact.title} (${artifact.kind})`,
            eventType: "artifact",
            timestamp: Date.now(),
            payload: artifact,
          });
        }

        currentMessages.push(
          new ToolMessage({
            content: resultStr,
            tool_call_id: tc.id ?? "",
          }),
        );
      } catch (err) {
        const errorMsg = err instanceof Error ? err.message : String(err);
        onEvent?.({
          agent: agentType,
          content: `工具 ${tc.name} 错误: ${errorMsg}`,
          eventType: "error",
          timestamp: Date.now(),
        });

        currentMessages.push(
          new ToolMessage({
            content: `Error: ${errorMsg}`,
            tool_call_id: tc.id ?? "",
          }),
        );
      }
    }

    // 内部 Tool 转交：把带 tool_calls 的 AIMessage 交回 Supervisor，由它路由到目标 Agent
    if (internalTarget) {
      onEvent?.({
        agent: agentType,
        content: `转交 ${internalTarget} Agent 处理`,
        eventType: "thought",
        timestamp: Date.now(),
      });
      return { content: "", delegation: aiMessage };
    }
  }

  // 超过最大 Tool 调用次数
  const fallback = "已达到最大工具调用次数限制，请简化请求。";
  onEvent?.({
    agent: agentType,
    content: fallback,
    eventType: "response",
    timestamp: Date.now(),
  });
  return { content: fallback };
}

/**
 * 创建 Agent 节点函数（供 LangGraph StateGraph 使用）
 */
export function createAgentNode(
  agentType: Exclude<AgentType, "supervisor">,
  onEvent?: (event: AgentEvent) => void,
  internalTools: StructuredToolInterface[] = [],
  onUsage?: UsageReporter,
  signal?: AbortSignal,
  config?: AgentConfig,
) {
  return async (state: GraphStateType): Promise<Partial<GraphStateType>> => {
    const { messages, userInput } = state;

    // 构建消息：system prompt + 历史 + 用户输入
    const systemPrompt = AGENT_SYSTEM_PROMPTS[agentType];
    const allowedTools = AGENT_TOOL_CONFIG[agentType] ?? [];
    const residentTools = getResidentToolsForAgent(agentType, internalTools);

    // 工具目录是单例，插件只加载一次；失败时降级为「只有转交工具」
    let catalog: ToolCatalog | null = null;
    try {
      catalog = await getToolCatalog();
    } catch (err) {
      console.error(`[LangGraph] ${agentType} 工具目录加载失败:`, err);
      onEvent?.({
        agent: agentType,
        content: `工具目录加载失败，本次只能用转交工具: ${err instanceof Error ? err.message : String(err)}`,
        eventType: "error",
        timestamp: Date.now(),
      });
    }

    const agentMessages: Array<SystemMessage | HumanMessage | AIMessage | ToolMessage> = [
      new SystemMessage(systemPrompt),
      ...messages
        .filter((m) => m instanceof HumanMessage || m instanceof AIMessage || m instanceof ToolMessage)
        .map((m) => {
          // 内部 Tool 的 AIMessage 带 tool_calls，但对应的 ToolMessage 不在图状态里。
          // 直接透传会被 Anthropic/OpenAI 拒绝（tool_use 缺少 tool_result），
          // 因此剥掉 tool_calls，只保留文本转交说明。
          if (!(m instanceof AIMessage) || !m.tool_calls?.length) return m;
          const summary = m.tool_calls.map((tc) => `${tc.name}(${JSON.stringify(tc.args)})`).join(", ");
          const text = typeof m.content === "string" ? m.content.trim() : "";
          return new AIMessage({
            content: text || `[转交 ${agentType} → 目标 Agent] ${summary}`,
            additional_kwargs: m.additional_kwargs,
          });
        }),
    ];

    // 如果有新的用户输入，追加到最后
    if (userInput) {
      agentMessages.push(new HumanMessage(userInput));
    }

    const { content, delegation } = await runAgentLoop(
      agentType,
      agentMessages,
      residentTools,
      catalog,
      allowedTools,
      onEvent,
      onUsage,
      signal,
      undefined,
      config,
    );

    // 内部 Tool 转交：不结束图，把带 tool_calls 的 AIMessage 交回 Supervisor 路由到目标 Agent
    if (delegation) {
      return {
        messages: [delegation],
        shouldEnd: false,
        iterationCount: state.iterationCount + 1,
      };
    }

    return {
      messages: [new AIMessage({ content, additional_kwargs: { agent: agentType } })],
      shouldEnd: true,
      iterationCount: state.iterationCount + 1,
    };
  };
}
