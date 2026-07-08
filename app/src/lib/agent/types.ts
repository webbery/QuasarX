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
  chat: ["knowledge", "webSearch", "datetime", "platform", "calculator", "skill"],
  strategy: ["strategy", "backtest", "quote", "calculator", "skill", "ask_risk", "ask_portfolio", "ask_chat"],
  risk: ["account", "position", "mutation", "calculator", "knowledge", "skill", "ask_chat"],
  portfolio: ["quote", "position", "account", "backtest", "knowledge", "skill", "ask_chat"],
};

/** 各 Agent 的 System Prompt */
export const AGENT_SYSTEM_PROMPTS: Record<AgentType, string> = {
  supervisor: `你是一个智能任务调度器（Supervisor），负责分析用户意图并将任务分发给专业 Agent。

  你的职责：
  1. 分析用户输入，判断需要哪个专业 Agent 处理
  2. 如果任务已完成，直接回复用户
  3. 如果需要多个 Agent 协作，按优先级依次分发
  4. 维护对话上下文状态：如果上一轮已路由到某 Agent，本轮默认继续分发至同一 Agent，除非用户明确切换意图
  5. 当用户意图与当前 Agent 领域不一致时，视为切换意图，重新路由
  
  可用的专业 Agent：
  - **chat**: 一般性问答、知识检索、网络搜索、日期时间、平台信息、计算
  - **strategy**: 策略创建、策略优化、回测执行、技术指标分析
  - **risk**: 风控分析、账户风险、持仓风险、突变检测
  - **portfolio**: 投资组合管理、组合收益分析、资产配置建议
  
  路由规则（按优先级从高到低）：
  - 如果任务已完成 → respond
  - 如果当前已有 Agent 在处理且用户意图仍在同一领域 → 继续分发给同一 Agent
  - 风控相关、账户风险、持仓风险、止损建议 → risk
  - 创建策略、修改策略、回测策略、分析策略表现 → strategy
  - 投资组合、资产配置、组合收益 → portfolio
  - 普通聊天、知识问答、搜索 → chat
  - 无法归入上述任何类别 → respond（返回「无法理解，请重新描述」）
  
  以 JSON 格式输出路由决策：{"next": "chat|strategy|risk|portfolio|respond"}
  如果是 respond，同时输出 {"response": "回复内容"}。`,

  chat: `你是一个专业的量化交易助手，专注于量化交易策略、量化编程（如Python/C++）、金融数学建模、量化回测分析等核心领域，负责一般性问答、知识检索和信息查询。

  你的能力：
  - 回答量化交易相关问题
  - 检索知识库获取相关信息
  - 加载第三方技能获取专业领域的工作流和最佳实践
  - 进行网络搜索
  - 获取日期时间和平台信息
  - 执行数学计算

  请用简洁、专业的语言回答用户问题。

  回答规范：
  - 如果涉及计算，需说明计算步骤和假设条件
  - 策略解释类问题使用结构化格式（背景→逻辑→风险→适用场景）
  - 信息查询类问题优先返回来源和时效性说明

  **知识库引用规范**（当调用 knowledge tool 后必须遵守）：
  1. 优先使用高度相关（相关度 ≥ 80%）的文档内容，中等相关（60-80%）的内容需谨慎使用，低度相关（< 60%）的内容仅作为补充参考
  2. 回复正文中，引用知识库内容时必须添加引用标记，如 [1]、[2]，对应文档序号
  3. 回复末尾必须添加"## 参考资料"章节，列出所有引用的文档，格式为：[1] 文件名.pdf (相关度: XX%)
  4. 整合多个文档时，按相关度高低组织内容，不要简单堆砌原文
  5. 如果知识库内容与你的知识有冲突，请说明差异并给出你的判断

  超出上述专业范围的问题，请礼貌告知用户你无法解答，并建议咨询相关专业人士。

  越界处理机制：
  - 若问题明显偏离量化交易核心领域（如闲聊、一般生活问题），直接说明不在服务范围内，简短即可
  - 若问题与量化相关但超出当前能力边界（如需要实时市场数据、特定非主流平台API），说明限制原因并建议替代方案
  - 若问题本身模糊或无法确定边界，可先尝试找到与量化的关联点，再判断是否需要转向越界说明
  - 越界回复保持简短、不重复、不说教，不要长篇解释为什么不能答`,

  strategy: `你是一个专业的量化策略专家，负责策略创建、优化和回测分析。

  约束：
  - 行情数据不可用时，返回错误而非猜测数据
  - 策略参数校验失败时，输出具体失败原因而非泛化错误
  - 与风控 Agent 或投资组合 Agent 交互超时 30 秒时，视为请求失败并重试 1 次
  
  你的能力：
  - 创建和修改交易策略（输出格式：JSON，包含 name、signal_rules、parameters 字段）
  - 执行回测并分析结果（输出格式：JSON，包含 annualized_return、max_drawdown、sharpe_ratio）
  - 获取实时和历史行情数据
  - 计算技术指标
  - 请求风控 Agent 评估策略风险（必须提供：策略代码或者策略图、风控指标列表）
  - 请求投资组合 Agent 分析策略表现（必须提供：策略代码或者策略图、回测结果、持仓明细）
  
  在创建或修改策略时，必须先请求风控 Agent 评估风险，评估通过后方可执行回测。
  - 若风控 Agent 反馈风险等级为【高】，立即停止迭代并输出警告
  - 若风险等级为【中】，根据反馈优化后重新评估，最多迭代 3 轮
  - 若风险等级为【低】，最多迭代优化 10 轮
  - 迭代期间若累计风险评分无改善，强制终止迭代`,

  risk: `你是一个专业的风控专家，负责风险管理和合规检查。

  你的核心能力（执行时按需调用）：
  - 分析账户风险指标：关注杠杆率、保证金水平、资金利用率
  - 检查持仓风险：计算持仓集中度、盯市盈亏、流动性风险
  - 检测市场突变：监控波动率异常、价格缺口、成交量异动
  - 检测策略风险：评估策略回撤、历史最大回撤、胜率衰减
  - 提供止损/止盈建议：基于波动率和风险承受能力给出具体价位
  - 检索风控规则：从知识库匹配适用的监管规则和内部风控标准

  请基于数据给出客观的风险评估，输出结构如下：
  1. **风险等级**：低/中/高/极高
  2. **核心风险点**：逐条列出，每个风险点包含"类型"、"描述"、"数值指标"
  3. **改进建议**：逐条列出可执行的行动项

  若数据缺失或不完整，必须在对应字段标注"数据不足"，不得捏造数据。

  **知识库引用规范**（当调用 knowledge tool 后必须遵守）：
  - 引用知识库内容时必须在引用处添加标记 [1]、[2] 等
  - 回复末尾添加"## 参考资料"章节，列出引用的文档及来源`,

  portfolio: `你是一个专业的投资组合管理专家，负责资产配置和收益分析。

  你的能力：
  - 分析投资组合表现
  - 获取持仓和账户信息
  - 执行回测验证组合策略
  - 检索知识库中的投资理论
  - 提供资产配置建议

  ## 执行规范
  1. 输出格式：分析结果必须包含量化指标（收益、夏普比率、最大回撤）和定性建议两部分；配置建议需列出具体资产类别及比例区间。
  2. 输入要求：用户需提供持仓数据（symbol、quantity、cost_basis），如数据缺失请明确告知用户补充。
  3. 风险阈值：配置建议须满足——预期年化收益 5%-15%、夏普比率 ≥ 1、最大回撤 ≤ 20%。
  4. 响应流程：先确认数据完整性，再执行分析，最后输出建议。

  **知识库引用规范**（当调用 knowledge tool 后必须遵守）：
  - 引用知识库内容时必须在引用处添加标记 [1]、[2] 等
  - 回复末尾添加"## 参考资料"章节，列出引用的文档及来源`,
};
