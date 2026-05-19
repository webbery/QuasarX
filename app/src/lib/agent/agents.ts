/**
 * 四个专业 Agent 定义
 *
 * 每个 Agent 包含：
 * - system prompt
 * - 专属 Tool 集合
 * - createNode() 方法，返回 LangGraph 图节点
 */

import { AIMessage, HumanMessage, SystemMessage, ToolMessage } from "@langchain/core/messages";
import { StructuredToolInterface } from "@langchain/core/tools";
import { createLLM, getAgentConfig, AgentConfig } from ".";

// Tool 导入
import { quoteTool } from "../tools/quote";
import { accountTool } from "../tools/account";
import { positionTool } from "../tools/position";
import { datetimeTool } from "../tools/datetime";
import { platformTool } from "../tools/platform";
import { knowledgeTool } from "../tools/knowledge";
import { webSearchTool } from "../tools/webSearch";
import { strategyTool } from "../tools/strategy";
import { backtestTool } from "../tools/backtest";
import { mutationTool } from "../tools/mutation";
import { calculatorTool } from "../tools/calculator";

import { AgentType, AGENT_SYSTEM_PROMPTS, AGENT_TOOL_CONFIG, GraphStateType, AgentEvent } from "./types";

/** Tool 名称到 Tool 实例的映射 */
const TOOL_REGISTRY: Record<string, StructuredToolInterface> = {
  quote: quoteTool,
  account: accountTool,
  position: positionTool,
  datetime: datetimeTool,
  platform: platformTool,
  knowledge: knowledgeTool,
  webSearch: webSearchTool,
  strategy: strategyTool,
  backtest: backtestTool,
  mutation: mutationTool,
  calculator: calculatorTool,
};

/**
 * 根据 Agent 类型获取其专属 Tools
 * Chat Agent 的工具可以被其他 Agent 通过内部 Tool 间接使用
 */
export function getToolsForAgent(agentType: AgentType, internalTools: StructuredToolInterface[] = []): StructuredToolInterface[] {
  const toolNames = AGENT_TOOL_CONFIG[agentType];
  const tools: StructuredToolInterface[] = [];

  for (const name of toolNames) {
    // 内部 Tool（ask_xxx）
    if (name.startsWith("ask_")) {
      const internal = internalTools.find((t) => t.name === name);
      if (internal) tools.push(internal);
      continue;
    }
    // 标准 Tool
    const tool = TOOL_REGISTRY[name];
    if (tool) tools.push(tool);
  }

  return tools;
}

/**
 * 创建 LLM 实例（带 Tool Calling 绑定）
 */
function createAgentLLM(agentType: AgentType, tools: StructuredToolInterface[]) {
  const config = getAgentConfig();
  if (!config) {
    throw new Error("LLM 配置未设置，请先在设置中配置 Agent");
  }

  const llm = createLLM(config);
  // 绑定 Tool Calling
  return llm.bindTools(tools);
}

/**
 * Agent 执行函数：LLM → Tool Calling 循环 → 返回最终消息
 */
async function runAgentLoop(
  agentType: AgentType,
  messages: Array<SystemMessage | HumanMessage | AIMessage | ToolMessage>,
  tools: StructuredToolInterface[],
  onEvent?: (event: AgentEvent) => void,
  maxToolCalls = 30,
): Promise<string> {
  let llm: any;
  try {
    llm = createAgentLLM(agentType, tools);
  } catch (err) {
    console.error(`[LangGraph] ${agentType} 创建 LLM 失败:`, err);
    onEvent?.({
      agent: agentType,
      content: `LLM 初始化失败: ${err instanceof Error ? err.message : String(err)}`,
      eventType: "error",
      timestamp: Date.now(),
    });
    return "AI 服务不可用，请检查 LLM 配置。";
  }

  let currentMessages = [...messages];
  let toolCallCount = 0;

  while (toolCallCount < maxToolCalls) {
    let response: any;
    try {
      response = await llm.invoke(currentMessages);
    } catch (err) {
      console.error(`[LangGraph] ${agentType} LLM 调用失败:`, err);
      onEvent?.({
        agent: agentType,
        content: `LLM 调用失败: ${err instanceof Error ? err.message : String(err)}`,
        eventType: "error",
        timestamp: Date.now(),
      });
      return "AI 服务暂时不可用，请稍后重试。";
    }

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
    const toolCalls = response.tool_calls;
    if (!toolCalls || toolCalls.length === 0) {
      // 无 Tool 调用，返回最终回复
      const content = typeof response.content === "string" ? response.content : response.content.map((c: any) => (c as any).text || "").join("");
      onEvent?.({
        agent: agentType,
        content,
        eventType: "response",
        timestamp: Date.now(),
      });
      return content;
    }

    // 执行 Tool 调用
    toolCallCount += toolCalls.length;
    currentMessages.push(response as AIMessage);

    for (const tc of toolCalls) {
      onEvent?.({
        agent: agentType,
        content: `调用工具: ${tc.name}(${JSON.stringify(tc.args)})`,
        eventType: "tool_call",
        timestamp: Date.now(),
      });

      const tool = tools.find((t) => t.name === tc.name);
      if (!tool) {
        currentMessages.push(
          new ToolMessage({
            content: `Tool "${tc.name}" 不存在`,
            tool_call_id: tc.id ?? "",
          }),
        );
        continue;
      }

      try {
        const result = await tool.invoke(tc.args);
        const resultStr = typeof result === "string" ? result : JSON.stringify(result);

        onEvent?.({
          agent: agentType,
          content: `工具 ${tc.name} 返回: ${resultStr.substring(0, 500)}`,
          eventType: "tool_result",
          timestamp: Date.now(),
        });

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
  }

  // 超过最大 Tool 调用次数
  const fallback = "已达到最大工具调用次数限制，请简化请求。";
  onEvent?.({
    agent: agentType,
    content: fallback,
    eventType: "response",
    timestamp: Date.now(),
  });
  return fallback;
}

/**
 * 创建 Agent 节点函数（供 LangGraph StateGraph 使用）
 */
export function createAgentNode(
  agentType: Exclude<AgentType, "supervisor">,
  onEvent?: (event: AgentEvent) => void,
  internalTools: StructuredToolInterface[] = [],
) {
  return async (state: GraphStateType): Promise<Partial<GraphStateType>> => {
    const { messages, userInput } = state;

    // 构建消息：system prompt + 历史 + 用户输入
    const systemPrompt = AGENT_SYSTEM_PROMPTS[agentType];
    const tools = getToolsForAgent(agentType, internalTools);

    const agentMessages: Array<SystemMessage | HumanMessage | AIMessage | ToolMessage> = [
      new SystemMessage(systemPrompt),
      ...messages.filter((m) => m instanceof HumanMessage || m instanceof AIMessage || m instanceof ToolMessage),
    ];

    // 如果有新的用户输入，追加到最后
    if (userInput) {
      agentMessages.push(new HumanMessage(userInput));
    }

    const response = await runAgentLoop(agentType, agentMessages, tools, onEvent);

    return {
      messages: [new AIMessage({ content: response, additional_kwargs: { agent: agentType } })],
      shouldEnd: true,
      iterationCount: state.iterationCount + 1,
    };
  };
}
