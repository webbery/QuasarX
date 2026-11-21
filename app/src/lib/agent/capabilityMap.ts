/**
 * 能力地图（规划阶段的认知底物）
 *
 * 为什么要有这张图：
 * 1. 工具目录是**平的** —— 17 个工具平铺，模型靠关键词猜「我要的工具叫什么名字」。
 *    能力地图是**分层的**：先认域（我要做的是行情还是计量），再取域内工具。
 *    「下载 600584 行情再做协整检验」这种复合请求，靠域切分比靠字符串打分可靠得多。
 * 2. 边界要**能被看见**：域卡片里的 limits / notCovered 是硬性限制的声明，
 *    与代码里的门禁（agents.ts 的越权拒绝、tools 层的副作用确认）一一对应。
 * 3. 能力不足要有**显式出口**：notCovered 里的主题对应的就是 report_gap 的登记项，
 *    模型不该用相近工具硬凑。
 *
 * 域 → 工具的划分原则：
 * - 一个工具可以属于多个域（域是「视角」，不是分区）——quote 同时属于行情域和组合域。
 * - 域的粒度以「同一批动作共享同一条限制」为准：只读的工具放一起，
 *   带副作用的工具单独成域（market_data 的删除动作需要确认，strategy 的写入口需要提案）。
 *
 * 与 AGENT_TOOL_CONFIG 的关系：
 * - AGENT_TOOL_CONFIG 是**工具级白名单**，是「能不能启用」的唯一事实源。
 * - 本文件是**域级视图**，从白名单反推：某个 Agent 的白名单落在哪些域上。
 *   反向校验（每个白名单工具都必须归属至少一个域）由 checkCapabilityMap 保证。
 */

import type { AgentType } from "./types"

/** 能力域 */
export interface CapabilityDomain {
  /** 域 id（模型用它做 search(action="domain") 的检索键） */
  id: string
  /** 域标题 */
  title: string
  /** 适用场景：一句话说清「什么事归我管」 */
  scenarios: string
  /** 硬性限制：与代码里的门禁一一对应，不是装饰 */
  limits: string[]
  /** 该域涉及的工具（真实 Tool name，与 src/lib/tools/*.ts 的 name 字段一致） */
  tools: string[]
  /** 明确不覆盖的主题。routeTo 有值表示该转交给谁；缺省表示系统整体没有这个能力 */
  notCovered?: Array<{ topic: string; routeTo?: AgentType }>
  /** 前置条件：不满足时工具应返回 precondition_failed */
  requires?: string[]
  /** 副作用动作（"tool.action"）—— 这些动作不会直接执行，要用户确认 */
  mutations?: string[]
  /** 意图关键词：模糊检索兜底用（同义说法） */
  keywords?: string[]
}

/**
 * 能力域清单
 *
 * 新增工具时的规矩：先进某个域（domains），再由 checkCapabilityMap 校验；
 * 不要只加工具不加域 —— 那会让它成为「检索不到的能力」。
 */
export const CAPABILITY_DOMAINS: CapabilityDomain[] = [
  {
    id: "market_data",
    title: "行情与基础数据",
    scenarios: "下载/查询/导出行情与财务数据、股票·指数·期货列表、实时价格快照",
    limits: [
      "删除类动作（delete_quote / delete_finance）必须由用户确认后执行",
      "无逐笔成交与 Level-2 行情",
      "下载是异步任务，提交后用 list_tables 核对结果",
    ],
    tools: ["data_manager", "quote"],
    mutations: ["data_manager.delete_quote", "data_manager.delete_finance"],
    notCovered: [
      { topic: "实盘下单 / 委托报单" },
      { topic: "Level-2 逐笔与盘口" },
    ],
    keywords: ["行情", "数据", "下载", "K线", "财务", "股票列表", "期权链", "期货"],
  },
  {
    id: "strategy_backtest",
    title: "策略与回测",
    scenarios: "创建/修改策略图、查询策略与历史版本、执行回测、查看策略绩效",
    limits: [
      "修改策略图一律走提案模式：只产出 diff，用户确认后才写入",
      "删除策略图需要用户确认",
      "策略图必须包含 input / signal / portfolio / execution 四类节点",
      "回测是分钟级异步任务，先推进度卡再等待",
    ],
    tools: ["strategy", "backtest"],
    mutations: ["strategy.update_strategy", "strategy.delete_strategy"],
    notCovered: [{ topic: "策略部署到实盘 service", routeTo: "strategy" }],
    keywords: ["策略", "回测", "策略图", "节点", "参数", "版本", "绩效"],
  },
  {
    id: "quant_analytics",
    title: "统计与计量分析",
    scenarios: "EMD 分解、协整/Granger、CUSUM/变点检测、PCA、波动率、蒙特卡洛、协方差诊断",
    limits: [
      "只读：不写库、不改策略、不动仓位",
      "需要先有行情序列或回测结果作为输入",
      "多标的相关性分析至少需要 2 个标的",
      "所有指标必须来自工具返回，不得插值或估算",
    ],
    tools: ["analysis", "performance"],
    requires: ["本地已有行情数据或回测结果"],
    notCovered: [{ topic: "模型训练与实盘预测", routeTo: "strategy" }],
    keywords: ["协整", "波动率", "突变", "变点", "CUSUM", "EMD", "PCA", "蒙特卡洛", "夏普", "回撤"],
  },
  {
    id: "option_derivatives",
    title: "期权与衍生品",
    scenarios: "期权定价（BS/MC/二叉树）、希腊字母、隐含波动率与 IV 曲面、期权组合收益（payoff）",
    limits: [
      "需要合约参数快照（执行价/到期/利率），不做实时曲面拟合",
      "组合收益是本地近似计算（未考虑时间价值衰减等细节）",
      "不做撮合与行权",
    ],
    tools: ["option_pricing"],
    notCovered: [{ topic: "期权实盘交易" }],
    keywords: ["期权", "定价", "希腊", "隐波", "IV", "波动率曲面", "Delta", "Gamma", "组合收益", "盈亏平衡"],
  },
  {
    id: "risk_monitoring",
    title: "风控指标与断路器",
    scenarios: "账户风险敞口、止损线、单日限额、断路器状态、VaR/CVaR 等风控指标",
    limits: ["只评估不执行：不改仓位、不下单、不改风控配置"],
    tools: ["risk_monitor"],
    notCovered: [{ topic: "修改风控配置", routeTo: "risk" }],
    keywords: ["风控", "风险", "止损", "敞口", "断路器", "VaR", "CVaR", "限额"],
  },
  {
    id: "account_funds",
    title: "账户资金与保证金",
    scenarios: "账户权益、可用资金、杠杆倍数、保证金占用",
    limits: ["只读：不能充值、调仓或调整保证金"],
    tools: ["account"],
    keywords: ["账户", "资金", "权益", "杠杆", "保证金"],
  },
  {
    id: "position_holdings",
    title: "持仓与集中度",
    scenarios: "持仓明细、持仓集中度、仓位结构",
    limits: ["只读：调仓属于策略域，本域只做现状描述"],
    tools: ["position"],
    requires: ["用户提供的持仓数据，或已连接的账户"],
    keywords: ["持仓", "仓位", "集中度", "头寸"],
  },
  {
    id: "order_history",
    title: "订单与成交流水",
    scenarios: "订单查询、成交记录、资金流水",
    limits: ["只读：系统不提供下单通道"],
    tools: ["order_query"],
    keywords: ["订单", "成交", "流水", "交易查询"],
  },
  {
    id: "knowledge_web",
    title: "知识库与联网",
    scenarios: "本地知识库检索、联网搜索",
    limits: [
      "引用知识库内容必须标注 [n] 与「## 参考资料」章节，并给出相关度",
      "联网结果必须说明来源与时效性",
      "当前日期时间用 script_exec 域里的 javascript 工具（new Date()），不要编造",
    ],
    tools: ["knowledge", "web_search"],
    keywords: ["知识库", "文档", "联网", "搜索", "新闻"],
  },
  {
    id: "render_ui",
    title: "图表与交互展示",
    scenarios:
      "回测收益/回撤曲线、时序对比、相关性热力图、表格、指标卡、进度卡；以及带参数滑动条、下拉选择、开关、数值输入的可调控件",
    limits: [
      "所有图表数值必须来自工具返回，禁止编造",
      "回测曲线用 performance 构建器由系统取数，不要自己喂数据",
      "可调控件的取值范围与选项同样来自 builder 定义，不自造区间",
    ],
    tools: ["ui"],
    keywords: ["图表", "画图", "可视化", "曲线", "热力图", "表格", "指标卡", "进度", "滑动条", "输入框", "控件"],
  },
  {
    id: "script_exec",
    title: "脚本执行（计算 / 时间 / 系统信息）",
    scenarios:
      "精确数值计算、统计/数值加工、当前日期时间、系统环境信息 —— 模型生成脚本，工具在受限沙箱里执行",
    limits: [
      "沙箱内没有 DOM、文件、网络与 Node API，只能算和看环境",
      "脚本默认超时 5 秒（上限 15 秒），长任务请拆小",
      "不提供汇率表：汇率必须来自实时行情或联网结果，不能写死",
      "期权定价与组合收益归 option_derivatives 域（option_pricing 工具），不要在这里手搓",
    ],
    tools: ["javascript"],
    notCovered: [{ topic: "需要访问本地文件或网络的批处理脚本" }],
    keywords: ["计算", "换算", "百分比", "复利", "标准差", "时间", "日期", "系统", "平台", "环境", "脚本"],
  },
]

/**
 * Agent → 能力域
 *
 * 约束：白名单里的每个工具都必须能被这里的某个域覆盖（checkCapabilityMap 会校验）。
 * 一个域可以只覆盖白名单的一部分 —— 渲染时按交集取工具。
 */
export const AGENT_DOMAINS: Record<AgentType, string[]> = {
  supervisor: [],
  chat: ["knowledge_web", "script_exec", "account_funds", "order_history", "render_ui"],
  strategy: ["strategy_backtest", "market_data", "script_exec", "quant_analytics", "render_ui"],
  risk: ["risk_monitoring", "account_funds", "position_holdings", "script_exec", "knowledge_web", "order_history", "quant_analytics", "render_ui"],
  portfolio: ["market_data", "position_holdings", "account_funds", "strategy_backtest", "knowledge_web", "order_history", "risk_monitoring", "quant_analytics", "render_ui"],
  analysis: ["quant_analytics", "option_derivatives", "market_data", "script_exec", "render_ui"],
}

/**
 * 各 Agent 的**能力白名单**
 *
 * 语义：
 * - `ask_*` 开头的是内部转交工具 → **常驻绑定**，随时可调
 * - 其余是真实工具名 → **可加载**，要靠 search(action="contract") 加载契约后才出现在 tools 数组里
 *
 * key 必须与 `src/lib/tools/*.ts` 里的 name 字段**完全一致**，否则会被静默丢弃 ——
 * 之前这里的 "webSearch" 与真实工具名 "web_search" 对不上，chat Agent 因此一直没有联网搜索。
 *
 * 它和 AGENT_DOMAINS 是一对：白名单管「能不能加载」，域管「怎么理解这件事归谁管」。
 * 两者的一致性由 checkCapabilityMap 保证（白名单里的每个工具都要落在某个域上）。
 */
export const AGENT_TOOL_CONFIG: Record<AgentType, string[]> = {
  supervisor: [], // Supervisor 只路由，不直接使用 Tool
  chat: [
    "knowledge", "web_search", "javascript",
    "account", "order_query",
    "ask_strategy",
  ],
  strategy: [
    "strategy", "backtest", "quote", "javascript",
    "data_manager", "analysis", "performance",
    "ask_risk", "ask_portfolio", "ask_chat",
  ],
  risk: [
    "account", "position", "javascript", "knowledge",
    "risk_monitor", "order_query", "performance", "analysis",
    "ask_chat",
  ],
  portfolio: [
    "quote", "position", "account", "backtest", "knowledge",
    "data_manager", "order_query", "risk_monitor", "performance",
    "ask_chat",
  ],
  analysis: [
    "analysis", "option_pricing", "data_manager", "performance", "javascript",
  ],
}

/** 取某个域 */
export function getDomain(id: string): CapabilityDomain | undefined {
  return CAPABILITY_DOMAINS.find((d) => d.id === id)
}

/** 取某个 Agent 可见的域（按 AGENT_DOMAINS 声明顺序） */
export function domainsForAgent(agent: AgentType): CapabilityDomain[] {
  return (AGENT_DOMAINS[agent] ?? [])
    .map((id) => getDomain(id))
    .filter((d): d is CapabilityDomain => !!d)
}

/**
 * 常驻工具：每个 Agent 都直接绑定、不需要 contract 加载。
 * 它们不进白名单（白名单管的是「能加载什么」），但要显示在能力地图里。
 */
const RESIDENT_TOOLS = new Set(["ui"])

/** 域内属于该 Agent 白名单（或常驻）的工具 */
export function effectiveTools(agent: AgentType, domain: CapabilityDomain, allowed: Set<string>): string[] {
  return domain.tools.filter((t) => allowed.has(t) || RESIDENT_TOOLS.has(t))
}

/** 工具属于哪些域 */
export function domainsForTool(toolName: string): CapabilityDomain[] {
  return CAPABILITY_DOMAINS.filter((d) => d.tools.includes(toolName))
}

/**
 * 该动作是否是「需用户确认的副作用动作」
 *
 * 命中即表示：工具不该直接执行，而应产出待确认动作（见 tools/data.ts、tools/strategy.ts）。
 */
export function isMutationAction(toolName: string, action: string): boolean {
  const key = `${toolName}.${action}`
  return CAPABILITY_DOMAINS.some((d) => (d.mutations ?? []).includes(key))
}

/**
 * 该工具的能力归属说明（越权拒绝时告诉模型「这事归谁管」）
 *
 * 找不到归属 → 系统整体没有这个能力 → 应该 report_gap，而不是硬凑。
 */
export function capabilityOwner(toolName: string): {
  domains: CapabilityDomain[]
  routeTo?: AgentType
} {
  const domains = domainsForTool(toolName)
  for (const d of domains) {
    for (const nc of d.notCovered ?? []) {
      if (nc.routeTo) return { domains, routeTo: nc.routeTo }
    }
  }
  return { domains }
}

/**
 * 渲染能力地图（注入 Agent 的 system prompt）
 *
 * 只给「域 id / 场景 / 限制 / 工具名」，**不给 JSON Schema** ——
 * 完整契约按需加载，避免前期上下文污染。
 */
export function renderCapabilityMap(agent: AgentType, allowed: Set<string>): string {
  const domains = domainsForAgent(agent).filter(
    (d) => effectiveTools(agent, d, allowed).length > 0,
  )
  if (domains.length === 0) return ""

  const lines: string[] = [
    "【能力地图】你当前可用的能力域。先认域，再取域内工具：",
  ]

  for (const d of domains) {
    lines.push(`- ${d.id} · ${d.title}`)
    lines.push(`  适用：${d.scenarios}`)
    if (d.limits.length) lines.push(`  限制：${d.limits.join("；")}`)
    if (d.requires?.length) lines.push(`  前置：${d.requires.join("；")}`)
    const tools = effectiveTools(agent, d, allowed)
    lines.push(`  工具：${tools.join(", ")}`)
    const gaps = (d.notCovered ?? []).filter((nc) => !nc.routeTo)
    if (gaps.length) {
      lines.push(`  不覆盖：${gaps.map((g) => g.topic).join("；")}（这类需求用 report_gap 登记，别用相近工具硬凑）`)
    }
  }

  lines.push("")
  lines.push("取工具的顺序：search(action=\"domain\", keyword=\"<域 id>\") 看域内动作 → search(action=\"contract\", keyword=\"<工具名>\") 加载契约 → 下一轮即可调用。")
  lines.push("地图里没有的能力不要硬凑：用 report_gap 登记缺口，说明哪一步做不到、原因和替代方案。")

  return lines.join("\n")
}

/**
 * 一致性自检
 *
 * 返回问题清单（空数组 = 通过）。在开发模式和控制台各跑一次，防止地图与真实工具漂移。
 *
 * @param allowedByAgent 每个 Agent 的工具白名单（AGENT_TOOL_CONFIG）
 */
export function checkCapabilityMap(
  allowedByAgent: Record<string, string[]>,
): string[] {
  const issues: string[] = []

  // 1) 每个 Agent 的白名单工具必须归属至少一个该 Agent 可见的域
  for (const [agent, tools] of Object.entries(allowedByAgent)) {
    if (agent === "supervisor") continue
    const domains = domainsForAgent(agent as AgentType)
    const covered = new Set(domains.flatMap((d) => d.tools))
    const uncovered = tools.filter((t) => !t.startsWith("ask_") && !covered.has(t))
    if (uncovered.length) {
      issues.push(`[${agent}] 白名单里的工具没有对应的能力域：${uncovered.join(", ")}`)
    }
    // 2) 声明的域必须与白名单有交集，否则卡片是空的
    const allowed = new Set(tools)
    for (const d of domains) {
      if (effectiveTools(agent as AgentType, d, allowed).length === 0) {
        issues.push(`[${agent}] 能力域 ${d.id} 与白名单无交集（卡片会是空的）`)
      }
    }
  }

  // 3) 域 id 不能重复
  const seen = new Set<string>()
  for (const d of CAPABILITY_DOMAINS) {
    if (seen.has(d.id)) issues.push(`能力域 id 重复：${d.id}`)
    seen.add(d.id)
  }

  // 4) notCovered 里指向的 Agent 必须真实存在
  for (const d of CAPABILITY_DOMAINS) {
    for (const nc of d.notCovered ?? []) {
      if (nc.routeTo && !AGENT_DOMAINS[nc.routeTo]) {
        issues.push(`能力域 ${d.id} 的 notCovered 指向了不存在的 Agent：${nc.routeTo}`)
      }
    }
  }

  return issues
}

/** 工具目录的最小结构（用结构类型避免 capabilityMap ← toolCatalog 的循环依赖） */
export interface ToolNameSource {
  list(): Array<{ name: string; actions: string[] }>
}

/**
 * 特殊工具：不参与「域 ↔ 目录」一致性校验
 *
 * - ui：常驻工具（每个 Agent 都直接绑定），出现在 render_ui 域的卡片里，
 *   但刻意不进目录（不需要 contract 加载）
 * - skill：能力检索已由 agent 层的 search 工具统一承担，目录排除
 */
const SPECIAL_TOOLS = new Set(["ui", "skill", "search", "report_gap"])

/**
 * 完整自检：地图 ↔ 真实工具目录
 *
 * 查四类漂移：
 * - 域里写了目录里没有的工具（改名/删除后忘了改地图）
 * - 目录里有工具没进任何域（成了检索不到的能力）
 * - mutations 声明的动作在工具 schema 里不存在（门禁形同虚设）
 * - 白名单与域的覆盖关系断裂
 */
export function verifyCapabilityMap(
  source: ToolNameSource,
  allowedByAgent: Record<string, string[]>,
): string[] {
  const issues = checkCapabilityMap(allowedByAgent)
  const entries = source.list()
  const byName = new Map(entries.map((e) => [e.name, e]))

  // 1) 域里的工具必须真实存在（特殊工具除外）
  for (const d of CAPABILITY_DOMAINS) {
    for (const t of d.tools) {
      if (!byName.has(t) && !SPECIAL_TOOLS.has(t)) {
        issues.push(`能力域 ${d.id} 声明的工具「${t}」不在工具目录里（工具改名或删除后忘了同步地图？）`)
      }
    }
  }

  // 2) 目录里的每个工具必须至少属于一个域（特殊工具除外）
  const inDomain = new Set(CAPABILITY_DOMAINS.flatMap((d) => d.tools))
  for (const e of entries) {
    if (!inDomain.has(e.name) && !SPECIAL_TOOLS.has(e.name)) {
      issues.push(`工具「${e.name}」不属于任何能力域 —— 它对模型是不可发现的`)
    }
  }

  // 3) mutations 声明的动作必须真实存在于该工具的 action 枚举
  for (const d of CAPABILITY_DOMAINS) {
    for (const mutation of d.mutations ?? []) {
      const [toolName, action] = mutation.split(".")
      const entry = byName.get(toolName)
      if (!entry) continue
      if (!entry.actions.includes(action)) {
        issues.push(`能力域 ${d.id} 声明的副作用动作「${mutation}」在工具里不存在（工具的 action 枚举改了？）`)
      }
    }
  }

  return issues
}
