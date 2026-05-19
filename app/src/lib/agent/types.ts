import { Annotation } from "@langchain/langgraph";
import { BaseMessage } from "@langchain/core/messages";

/** Agent 类型标识 */
export type AgentType = "supervisor" | "chat" | "strategy" | "risk" | "portfolio";

/** Supervisor 路由决策 */
export type RouterDecision = "chat" | "strategy" | "risk" | "portfolio" | "respond";

/** Agent 工作流事件（推送到 UI 展示） */
export interface AgentEvent {
  /** 来源 Agent */
  agent: AgentType;
  /** 事件内容（思考过程 / Tool 调用 / 回复） */
  content: string;
  /** 事件类型 */
  eventType: "thought" | "tool_call" | "tool_result" | "response" | "error";
  /** 时间戳 */
  timestamp: number;
}

/** LangGraph State */
export const GraphState = Annotation.Root({
  /** 对话消息历史（包含用户输入和 Agent 回复） */
  messages: Annotation<BaseMessage[]>({
    reducer: (state, update) => state.concat(update),
    default: () => [],
  }),

  /** 当前活跃的 Agent（Supervisor 路由决策结果） */
  activeAgent: Annotation<AgentType | null>({
    reducer: (_state, update) => update,
    default: () => null,
  }),

  /** Supervisor 路由决策 */
  routerDecision: Annotation<RouterDecision | null>({
    reducer: (_state, update) => update,
    default: () => null,
  }),

  /** 当前迭代轮次（用于控制 Agent 间协作循环次数） */
  iterationCount: Annotation<number>({
    reducer: (_state, update) => update,
    default: () => 0,
  }),

  /** 最大迭代轮次（默认 50） */
  maxIterations: Annotation<number>({
    reducer: (_state, update) => update,
    default: () => 50,
  }),

  /** Agent 工作流事件列表（推送到前端展示） */
  agentEvents: Annotation<AgentEvent[]>({
    reducer: (state, update) => state.concat(update),
    default: () => [],
  }),

  /** 是否应该提前结束图循环（Agent 已产生回复时设置） */
  shouldEnd: Annotation<boolean>({
    reducer: (_state, update) => update,
    default: () => false,
  }),

  /** 最终回复内容（Supervisor 决定 respond 时填充） */
  finalResponse: Annotation<string>({
    reducer: (_state, update) => update,
    default: () => "",
  }),

  /** 对话 session ID（用于 checkpoint 隔离） */
  sessionId: Annotation<string>({
    reducer: (_state, update) => update,
    default: () => "default",
  }),

  /** 用户输入（每次新对话时设置） */
  userInput: Annotation<string>({
    reducer: (_state, update) => update,
    default: () => "",
  }),
});

export type GraphStateType = typeof GraphState.State;

/** 各 Agent 的 Tool 配置 */
export const AGENT_TOOL_CONFIG: Record<AgentType, string[]> = {
  supervisor: [], // Supervisor 只路由，不直接使用 Tool
  chat: ["knowledge", "webSearch", "datetime", "platform", "calculator"],
  strategy: ["strategy", "backtest", "quote", "calculator", "ask_risk", "ask_portfolio", "ask_chat"],
  risk: ["account", "position", "mutation", "calculator", "knowledge", "ask_chat"],
  portfolio: ["quote", "position", "account", "backtest", "knowledge", "ask_chat"],
};

/** 各 Agent 的 System Prompt */
export const AGENT_SYSTEM_PROMPTS: Record<AgentType, string> = {
  supervisor: `你是一个智能任务调度器（Supervisor），负责分析用户意图并将任务分发给专业 Agent。

你的职责：
1. 分析用户输入，判断需要哪个专业 Agent 处理
2. 如果任务已完成，直接回复用户
3. 如果需要多个 Agent 协作，依次分发

可用的专业 Agent：
- **chat**: 一般性问答、知识检索、网络搜索、日期时间、平台信息、计算
- **strategy**: 策略创建、策略优化、回测执行、技术指标分析
- **risk**: 风控分析、账户风险、持仓风险、突变检测
- **portfolio**: 投资组合管理、组合收益分析、资产配置建议

路由规则：
- 普通聊天、知识问答、搜索 → chat
- 创建策略、修改策略、回测策略、分析策略表现 → strategy
- 风控相关、账户风险、持仓风险、止损建议 → risk
- 投资组合、资产配置、组合收益 → portfolio
- 如果当前已经有 Agent 在处理，且需要继续 → 继续分发给对应 Agent
- 如果任务已完成 → respond

以 JSON 格式输出路由决策：{"next": "chat|strategy|risk|portfolio|respond"}
如果是 respond，同时输出 {"response": "回复内容"}。`,

  chat: `你是一个专业的量化交易助手，负责一般性问答、知识检索和信息查询。

你的能力：
- 回答量化交易相关问题
- 检索知识库获取相关信息
- 进行网络搜索
- 获取日期时间和平台信息
- 执行数学计算

请用简洁、专业的语言回答用户问题。`,

  strategy: `你是一个专业的量化策略专家，负责策略创建、优化和回测分析。

你的能力：
- 创建和修改交易策略
- 执行回测并分析结果
- 获取实时和历史行情数据
- 计算技术指标
- 请求风控 Agent 评估策略风险
- 请求投资组合 Agent 分析策略表现

在创建或修改策略时，建议先请求风控 Agent 评估，根据反馈进行优化。最多迭代优化 10 轮。`,

  risk: `你是一个专业的风控专家，负责风险管理和合规检查。

你的能力：
- 分析账户风险指标
- 检查持仓风险
- 检测市场突变
- 提供止损、止盈建议
- 检索知识库中的风控规则

请基于数据给出客观的风险评估，明确指出具体风险点和改进建议。`,

  portfolio: `你是一个专业的投资组合管理专家，负责资产配置和收益分析。

你的能力：
- 分析投资组合表现
- 获取持仓和账户信息
- 执行回测验证组合策略
- 检索知识库中的投资理论
- 提供资产配置建议

请基于数据给出科学的配置建议，平衡收益和风险。`,
};
