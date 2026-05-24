/**
 * Supervisor StateGraph
 *
 * 核心架构：
 * 1. Supervisor Node（LLM 路由决策）
 * 2. Chat / Strategy / Risk / Portfolio Agent Nodes
 * 3. 条件边（Conditional Edges）：根据 Supervisor 决策路由到对应 Agent
 * 4. Agent 完成后返回 Supervisor，形成循环
 * 5. Supervisor 决定 respond 时结束
 *
 * recursion_limit: 50
 */

import { StateGraph } from "@langchain/langgraph";
import { AIMessage, HumanMessage, SystemMessage, ToolMessage } from "@langchain/core/messages";
import { createLLM, getAgentConfig } from ".";

import { GraphState, GraphStateType, AgentType, RouterDecision, AGENT_SYSTEM_PROMPTS } from "./types";
import { createAgentNode } from "./agents";
import { internalTools, INTERNAL_TOOL_AGENT_MAP } from "./internalTools";
import { AgentEvent } from "./types";
import { IndexedDBSaver } from "./checkpointer";

/** Supervisor LLM（仅用于路由决策，不绑定 Tool） */
function createSupervisorLLM() {
  const config = getAgentConfig();
  if (!config) {
    throw new Error("LLM 配置未设置");
  }
  // Supervisor 需要较强的推理能力，使用稍低的 temperature
  const llm = createLLM({ ...config });
  // @ts-ignore - temperature 在创建后可修改
  llm.temperature = 0.2;
  return llm;
}

/**
 * Supervisor 路由决策 Node
 * 分析当前状态，决定下一步交给哪个 Agent，或直接回复
 */
async function supervisorNode(state: GraphStateType, emitEvent?: (event: AgentEvent) => void): Promise<Partial<GraphStateType>> {
  const { messages, userInput, iterationCount, maxIterations, shouldEnd } = state;

  // ★ 调试日志：打印收到的 state
  console.log('[LangGraph Supervisor Debug - State]', {
    messagesLength: messages.length,
    messagesPreview: messages.map(m => ({
      type: m.constructor.name,
      role: m._getType?.() || 'unknown',
      contentPreview: typeof m.content === 'string' ? m.content.substring(0, 80) : '[complex]',
      additional_kwargs: m.additional_kwargs,
    })),
    userInput: userInput?.substring(0, 80),
    iterationCount,
    shouldEnd,
    sessionId: state.sessionId,
  });

  // 调试：打印收到的 messages
  console.log('[LangGraph Supervisor] 接收到的 messages:', {
    count: messages.length,
    types: messages.map(m => m.constructor.name),
    preview: messages.slice(-4).map(m => {
      const content = typeof m.content === 'string' ? m.content.substring(0, 50) : '[complex]';
      return `${m.constructor.name}: ${content}`;
    }),
  });

  // 如果 Agent 已经产生回复，直接结束图循环
  if (shouldEnd) {
    emitEvent?.({
      agent: "supervisor",
      content: "检测到 Agent 已产生回复，提前结束图循环",
      eventType: "thought",
      timestamp: Date.now(),
    });
    return {
      routerDecision: "respond",
      activeAgent: null,
      finalResponse: "",
    };
  }

  // 构建 Supervisor 消息
  const systemPrompt = AGENT_SYSTEM_PROMPTS.supervisor;

  // 如果这是第一轮对话，添加用户输入
  const historyMessages = messages.filter(
    (m) => m instanceof HumanMessage || m instanceof AIMessage,
  );

  const supervisorMessages: Array<SystemMessage | HumanMessage | AIMessage> = [
    new SystemMessage(systemPrompt),
    ...historyMessages.slice(-20), // 只保留最近 20 条历史
  ];

  // 如果有新的用户输入
  if (userInput && iterationCount === 0) {
    supervisorMessages.push(new HumanMessage(userInput));
  }

  let llm: any;
  try {
    llm = createSupervisorLLM();
  } catch (err) {
    console.error('[LangGraph] Supervisor 创建 LLM 失败:', err);
    emitEvent?.({
      agent: "supervisor",
      content: `LLM 初始化失败: ${err instanceof Error ? err.message : String(err)}`,
      eventType: "error",
      timestamp: Date.now(),
    });
    return {
      routerDecision: "respond",
      activeAgent: null,
      finalResponse: "",
    };
  }

  let response: any;
  try {
    response = await llm.invoke(supervisorMessages);
  } catch (err) {
    console.error('[LangGraph] Supervisor LLM 调用失败:', err);
    emitEvent?.({
      agent: "supervisor",
      content: `LLM 调用失败: ${err instanceof Error ? err.message : String(err)}`,
      eventType: "error",
      timestamp: Date.now(),
    });
    return {
      routerDecision: "respond",
      activeAgent: null,
      finalResponse: "",
    };
  }

  // 解析路由决策
  // 提取 LLM 的实际文本输出
  // Anthropic 格式: [{ type: 'thinking', ... }, { type: 'text', text: '{"next": "chat"}' }]
  // OpenAI 格式: 直接是字符串
  let content = "";
  if (typeof response.content === "string") {
    content = response.content;
  } else if (Array.isArray(response.content)) {
    // 从 Anthropic 格式中提取 text 块（跳过 thinking 块）
    for (const block of response.content) {
      if (block.type === 'text' && block.text) {
        content = block.text;
        break;
      }
    }
  }

  console.log('[Supervisor] LLM 原始输出:', content.substring(0, 200))

  let decision: RouterDecision = "respond";
  let finalResponse = "";

  // 尝试解析 JSON 决策
  const jsonMatch = content.match(/\{[\s\S]*"next"\s*:\s*"([^"]+)"[\s\S]*\}/);
  if (jsonMatch) {
    try {
      const jsonStr = jsonMatch[0];
      const parsed = JSON.parse(jsonStr.replace(/```json\s*|\s*```/g, ""));
      decision = parsed.next as RouterDecision;
      if (decision === "respond" && parsed.response) {
        finalResponse = parsed.response;
      }
    } catch {
      // JSON 解析失败，默认 respond
      decision = "respond";
    }
  } else if (content.includes('"respond"') || content.includes('"next": "respond"')) {
    decision = "respond";
  } else if (content.includes('"chat"')) {
    decision = "chat";
  } else if (content.includes('"strategy"')) {
    decision = "strategy";
  } else if (content.includes('"risk"')) {
    decision = "risk";
  } else if (content.includes('"portfolio"')) {
    decision = "portfolio";
  }

  // 检查是否超过最大迭代次数
  if (iterationCount >= maxIterations) {
    decision = "respond";
    finalResponse = "已达到最大迭代次数限制。";
  }

  emitEvent?.({
    agent: "supervisor",
    content: `路由决策: ${decision}`,
    eventType: "thought",
    timestamp: Date.now(),
  });

  return {
    routerDecision: decision,
    activeAgent: decision === "respond" ? null : (decision as AgentType),
    finalResponse,
  };
}

/**
 * 路由函数：根据 Supervisor 决策路由到对应 Agent
 */
function routeToAgent(state: GraphStateType): string {
  const { routerDecision } = state;

  switch (routerDecision) {
    case "chat":
      return "chatAgent";
    case "strategy":
      return "strategyAgent";
    case "risk":
      return "riskAgent";
    case "portfolio":
      return "portfolioAgent";
    case "respond":
    default:
      return "__end__";
  }
}

/**
 * 检查是否需要路由到内部 Tool 对应的 Agent
 * 当 Agent 调用了 ask_xxx Tool 时，Supervisor 自动路由到对应 Agent
 */
function checkInternalToolRouting(state: GraphStateType): string {
  const { messages, iterationCount, maxIterations } = state;

  // 检查超过最大迭代
  if (iterationCount >= maxIterations) {
    return "supervisor"; // 返回 Supervisor 强制结束
  }

  // 检查最后一条 AI 消息是否有 ask_xxx Tool 调用
  const lastMessage = messages[messages.length - 1];
  if (lastMessage instanceof AIMessage && lastMessage.tool_calls) {
    for (const tc of lastMessage.tool_calls) {
      const targetAgent = INTERNAL_TOOL_AGENT_MAP[tc.name];
      if (targetAgent) {
        return `${targetAgent}Agent`;
      }
    }
  }

  // 没有内部 Tool 调用，返回 Supervisor 重新路由
  return "supervisor";
}

/**
 * 构建并编译 Supervisor StateGraph
 *
 * @param emitEvent 事件回调，用于向 UI 推送 Agent 工作流事件
 * @param checkpointer Checkpoint 持久化器（用于多轮对话上下文恢复）
 * @returns 编译后的图
 */
export function buildSupervisorGraph(
  emitEvent?: (event: AgentEvent) => void,
  checkpointer?: IndexedDBSaver,
) {
  const workflow = new StateGraph(GraphState);

  // === 添加节点 ===

  // Supervisor Node
  workflow.addNode("supervisor", async (state: GraphStateType) => {
    return supervisorNode(state, emitEvent);
  });

  // Chat Agent Node
  workflow.addNode("chatAgent", async (state: GraphStateType) => {
    const node = createAgentNode("chat", emitEvent, internalTools);
    return node(state);
  });

  // Strategy Agent Node
  workflow.addNode("strategyAgent", async (state: GraphStateType) => {
    const node = createAgentNode("strategy", emitEvent, internalTools);
    return node(state);
  });

  // Risk Agent Node
  workflow.addNode("riskAgent", async (state: GraphStateType) => {
    const node = createAgentNode("risk", emitEvent, internalTools);
    return node(state);
  });

  // Portfolio Agent Node
  workflow.addNode("portfolioAgent", async (state: GraphStateType) => {
    const node = createAgentNode("portfolio", emitEvent, internalTools);
    return node(state);
  });

  // === 添加边 ===

  // 入口 → Supervisor
  // @ts-ignore - LangGraph 类型推断需要，但节点名称是有效的
  workflow.addEdge("__start__", "supervisor");

  // Supervisor → 条件路由
  // @ts-ignore - LangGraph 类型推断需要，但节点名称是有效的
  workflow.addConditionalEdges("supervisor", routeToAgent, {
    chatAgent: "chatAgent",
    strategyAgent: "strategyAgent",
    riskAgent: "riskAgent",
    portfolioAgent: "portfolioAgent",
    __end__: "__end__",
  });

  // 各 Agent 完成后 → 检查内部 Tool 调用或返回 Supervisor
  // @ts-ignore - LangGraph 类型推断需要
  workflow.addConditionalEdges("chatAgent", checkInternalToolRouting, {
    supervisor: "supervisor",
    chatAgent: "chatAgent",
    strategyAgent: "strategyAgent",
    riskAgent: "riskAgent",
    portfolioAgent: "portfolioAgent",
  });

  // @ts-ignore - LangGraph 类型推断需要
  workflow.addConditionalEdges("strategyAgent", checkInternalToolRouting, {
    supervisor: "supervisor",
    chatAgent: "chatAgent",
    strategyAgent: "strategyAgent",
    riskAgent: "riskAgent",
    portfolioAgent: "portfolioAgent",
  });

  // @ts-ignore - LangGraph 类型推断需要
  workflow.addConditionalEdges("riskAgent", checkInternalToolRouting, {
    supervisor: "supervisor",
    chatAgent: "chatAgent",
    strategyAgent: "strategyAgent",
    riskAgent: "riskAgent",
    portfolioAgent: "portfolioAgent",
  });

  // @ts-ignore - LangGraph 类型推断需要
  workflow.addConditionalEdges("portfolioAgent", checkInternalToolRouting, {
    supervisor: "supervisor",
    chatAgent: "chatAgent",
    strategyAgent: "strategyAgent",
    riskAgent: "riskAgent",
    portfolioAgent: "portfolioAgent",
  });

  // 编译图
  return workflow.compile({
    checkpointer,
  });
}

/**
 * 运行 Supervisor 图
 *
 * @param userInput 用户输入
 * @param sessionId 会话 ID
 * @param checkpointer Checkpoint 持久化器
 * @param emitEvent 事件回调
 * @returns 最终状态
 */
export async function runSupervisorGraph(
  userInput: string,
  sessionId: string,
  checkpointer: IndexedDBSaver,
  emitEvent?: (event: AgentEvent) => void,
) {
  const graph = buildSupervisorGraph(emitEvent, checkpointer);

  // 调试：尝试从 checkpoint 恢复历史
  const debugConfig = { configurable: { thread_id: sessionId } };
  const savedTuple = await checkpointer.getTuple(debugConfig);
  if (savedTuple) {
    // ★ 详细日志：打印 checkpoint 中的消息
    const checkpointMessages = savedTuple.checkpoint.channel_values?.messages || [];
    console.log('[LangGraph Checkpoint] 找到历史 checkpoint:', {
      checkpointId: savedTuple.config.configurable?.checkpoint_id,
      messagesCount: checkpointMessages.length,
      messagesPreview: checkpointMessages.map((m: any) => ({
        type: m.lc?.id?.[m.lc.id.length - 1] || m.constructor?.name || 'unknown',
        contentPreview: typeof m.content === 'string' ? m.content.substring(0, 80) : '[complex]',
      })),
    });
  } else {
    console.log('[LangGraph Checkpoint] 未找到历史，首次对话');
  }

  const initialState: GraphStateType = {
    messages: [],
    activeAgent: null,
    routerDecision: null,
    iterationCount: 0,
    maxIterations: 50,
    agentEvents: [],
    shouldEnd: false,
    finalResponse: "",
    sessionId,
    userInput,
  };

  console.log('[LangGraph] 初始状态:', {
    initialMessagesLength: initialState.messages.length,
    userInput: initialState.userInput?.substring(0, 50),
    sessionId: initialState.sessionId,
  });

  const result = await graph.invoke(initialState, {
    configurable: {
      thread_id: sessionId,
    },
    recursionLimit: 50,
  });

  console.log('[LangGraph] 执行完成，最终状态:', {
    resultMessagesLength: result.messages?.length || 0,
    resultMessagesPreview: result.messages?.slice(-4).map((m: any) => ({
      type: m.constructor?.name || 'unknown',
      contentPreview: typeof m.content === 'string' ? m.content.substring(0, 80) : '[complex]',
    })),
    finalResponse: result.finalResponse?.substring(0, 80),
    shouldEnd: result.shouldEnd,
  });

  return result;
}
