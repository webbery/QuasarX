/**
 * 策略 Tool
 *
 * Agent 可通过此工具：
 * - 查询所有可用节点类型
 * - 获取单个节点的详细信息（参数、输入输出、示例）
 * - 获取当前策略图
 * - 更新/删除策略图
 * - 根据描述创建策略图
 */

import { tool } from "@langchain/core/tools"
import { z } from "zod"
import { getAllNodes, getNode, searchNodes } from "../nodes"
import { useHistoryStore } from "@/stores/history"

// === 策略图 CRUD（使用 historyStore 管理）===

/** 验证策略图 JSON 结构是否完整有效 */
function validateStrategyGraph(data: any): { valid: boolean; errors: string[] } {
  const errors: string[] = []

  // 1. 检查 data 是否为对象
  if (!data || typeof data !== "object") {
    return { valid: false, errors: ["策略图数据为空或格式错误"] }
  }

  // 2. 检查 nodes 数组
  if (!data.nodes || !Array.isArray(data.nodes) || data.nodes.length === 0) {
    errors.push("缺少 nodes 数组或 nodes 为空")
  } else {
    // 检查每个节点是否有 id 和 data.nodeType
    data.nodes.forEach((node: any, index: number) => {
      if (!node.id) errors.push(`节点[${index}] 缺少 id`)
      if (!node.data?.nodeType) errors.push(`节点[${index}] 缺少 data.nodeType`)
    })

    // 检查必需节点类型
    const nodeTypes = data.nodes.map((n: any) => n.data?.nodeType)
    if (!nodeTypes.includes("input")) errors.push("缺少必需节点类型: input (数据输入)")
    if (!nodeTypes.includes("signal")) errors.push("缺少必需节点类型: signal (信号生成)")
    if (!nodeTypes.includes("portfolio")) errors.push("缺少必需节点类型: portfolio (投资组合)")
    if (!nodeTypes.includes("execution")) errors.push("缺少必需节点类型: execution (交易执行)")
  }

  // 3. 检查 edges 数组
  if (!data.edges || !Array.isArray(data.edges)) {
    errors.push("缺少 edges 数组")
  } else {
    const nodeIds = new Set(data.nodes?.map((n: any) => n.id) || [])

    // 检查每条边的结构
    data.edges.forEach((edge: any, index: number) => {
      if (!edge.source) errors.push(`边[${index}] 缺少 source`)
      if (!edge.target) errors.push(`边[${index}] 缺少 target`)

      // 检查 source 和 target 是否指向存在的节点
      if (edge.source && !nodeIds.has(edge.source)) {
        errors.push(`边[${index}] 的 source "${edge.source}" 指向不存在的节点`)
      }
      if (edge.target && !nodeIds.has(edge.target)) {
        errors.push(`边[${index}] 的 target "${edge.target}" 指向不存在的节点`)
      }
    })
  }

  return { valid: errors.length === 0, errors }
}

/** 获取所有已保存策略 */
function listStrategies(): string {
  const historyStore = useHistoryStore()
  if (historyStore.strategies.length === 0) return "当前没有已保存的策略图"
  const lines = historyStore.strategies.map(s =>
    `- **${s.name}** (id: \`${s.id}\`，更新于 ${new Date(s.updatedAt).toLocaleString('zh-CN')})`
  )
  return `已保存的策略图（共 ${historyStore.strategies.length} 个）：\n${lines.join('\n')}`
}

/** 获取单个策略 */
async function getStrategy(id: string): Promise<string> {
  const historyStore = useHistoryStore()
  const strategy = historyStore.strategies.find(s => s.id === id)
  if (!strategy) return `未找到策略图 "${id}"`

  // 获取最新版本
  const latestVersion = historyStore.getLatestVersion(id)
  if (!latestVersion) return `策略图 "${strategy.name}" (${id}) 没有已保存的版本`

  // 加载流程图数据
  const flowData = await historyStore.loadVersionFlowData(latestVersion.id)
  if (!flowData) return `策略图 "${strategy.name}" (${id}) 的流程图数据加载失败`

  return `\`\`\`json\n${JSON.stringify(flowData, null, 2)}\n\`\`\``
}

/** 更新策略图 */
async function updateStrategy(id: string, data: any): Promise<string> {
  const historyStore = useHistoryStore()
  const strategy = historyStore.strategies.find(s => s.id === id)
  if (!strategy) return `未找到策略图 "${id}"，请先使用 create_strategy 创建`

  // 解析策略图数据
  const flowData = data.nodes && data.edges ? data : data.graph || data

  // 验证策略图结构
  const validation = validateStrategyGraph(flowData)
  if (!validation.valid) {
    return [
      `❌ 策略图 "${strategy.name}" (${id}) 更新失败：结构验证未通过`,
      ``,
      `**错误信息：**`,
      ...validation.errors.map(e => `- ${e}`)
    ].join('\n')
  }

  // 保存为新版本
  const versionId = await historyStore.addVersion(id, flowData, `Agent 更新 ${new Date().toLocaleString('zh-CN')}`)

  // 更新策略的 updatedAt
  await historyStore.touchStrategy(id)

  // 返回更新结果 + 策略图 JSON 内容（方便调试检查）
  return [
    `✅ 策略图 "${strategy.name}" (${id}) 已更新为版本 \`${versionId}\``,
    `- 节点数: ${flowData.nodes.length}`,
    `- 边数: ${flowData.edges.length}`,
    ``,
    `**策略图结构：**`,
    `\`\`\`json`,
    JSON.stringify(flowData, null, 2),
    `\`\`\``
  ].join('\n')
}

/** 删除策略图 */
async function deleteStrategy(id: string): Promise<string> {
  const historyStore = useHistoryStore()
  const strategy = historyStore.strategies.find(s => s.id === id)
  if (!strategy) return `未找到策略图 "${id}"`

  await historyStore.removeStrategy(id)
  return `策略图 "${strategy.name}" (${id}) 及其所有版本已删除`
}

/** 创建策略图 */
async function createStrategy(name: string, data: any): Promise<string> {
  const historyStore = useHistoryStore()

  // 解析策略图数据
  const flowData = data.nodes && data.edges ? data : data.graph || data

  // 验证策略图结构
  const validation = validateStrategyGraph(flowData)
  if (!validation.valid) {
    return [
      `❌ 策略图 "${name}" 创建失败：结构验证未通过`,
      ``,
      `**错误信息：**`,
      ...validation.errors.map(e => `- ${e}`),
      ``,
      `**请检查：**`,
      `- 策略图 JSON 格式是否完整`,
      `- 是否包含所有必需节点类型（Input、Signal、Portfolio、Execution）`,
      `- 所有边的 source/target 是否指向存在的节点`
    ].join('\n')
  }

  // 创建新策略
  const strategyId = await historyStore.addStrategy(name)

  // 保存为第一个版本
  const versionId = await historyStore.addVersion(strategyId, flowData, `初始版本 ${new Date().toLocaleString('zh-CN')}`)

  // 返回创建结果 + 策略图 JSON 内容（方便调试检查）
  return [
    `✅ 策略图 "${name}" 已创建`,
    `- 策略 ID: \`${strategyId}\``,
    `- 版本 ID: \`${versionId}\``,
    `- 节点数: ${flowData.nodes.length}`,
    `- 边数: ${flowData.edges.length}`,
    ``,
    `**策略图结构：**`,
    `\`\`\`json`,
    JSON.stringify(flowData, null, 2),
    `\`\`\``
  ].join('\n')
}

// === Signal 公式语法文档 ===

function getSignalSyntaxDoc(): string {
  return `# Signal 公式语法

Signal 节点使用类 C 表达式语法生成买卖信号，公式分为 \`buy\` 和 \`sell\` 两部分。

## 变量引用限制（重要！）

**公式中的变量名只能是当前 Signal 节点的前驱节点输出名。**

- 变量必须来自与 Signal 节点直接相连的上游节点
- 不能使用未连接的节点输出或任意变量名
- 变量名格式：\`{字母及下划线开头的变量名}\`，例如 \`ma_short\`
- 常见前驱节点输出示例：
  - Input 节点：\`close\`, \`volume\`
  - Function 节点：\`ma_short\`, \`std\`
  - ML 节点：\`prediction\`

**如何查看可用变量**：使用 \`action="get_node_info"\` 查看前驱节点的 \`outputs\` 字段。

## 变量引用格式
格式：\`变量名[时间索引]\`
- \`close[t]\` — 当前时刻收盘价
- \`MA_5[t-1]\` — 前1个时刻的5日均线
- \`ReturnRate[t-5]\` — 前5个时刻的收益率
- \`volume[t]\` — 当前时刻成交量

时间索引支持：
- \`[t]\` — 当前时刻（最新值）
- \`[t-N]\` — 前 N 个时刻（如 \`[t-1]\`, \`[t-5]\`）
- \`[N]\` — 绝对索引位置（如 \`[0]\`）

## 运算符
- **比较运算**: \`>\`, \`<\`, \`>=\`, \`<=\`, \`==\`, \`!=\`
- **算术运算**: \`+\`, \`-\`, \`*\`, \`/\`
- **逻辑运算**: \`and\`, \`or\`, \`not\` / \`!\` （优先级：\`or\` < \`and\` < \`not\`/ \`!\` < 比较运算）

## 截面函数（对股票池所有股票计算）
- \`topk(表达式, k)\` — 值最大的前 k 只股票返回 true
- \`bottomk(表达式, k)\` — 值最小的前 k 只股票返回 true
- \`rank(表达式)\` — 返回 0~1 的排名归一化值（最高 1.0，最低 0.0）
- \`zscore(表达式)\` — Z-Score 标准化：(x - mean) / std

## 典型示例

### CTA 趋势跟踪
\`\`\`
buy: topk(ReturnRate[t], 2)
sell: bottomk(ReturnRate[t], 2)
\`\`\`

### 均值回归
\`\`\`
buy: zscore(close[t]) < -2
sell: zscore(close[t]) > 2
\`\`\`

### 多因子选股
\`\`\`
buy: rank(ReturnRate[t]) > 0.7 and rank(volume[t]) > 0.5
sell: rank(ReturnRate[t]) < 0.3
\`\`\`

### 动量突破
\`\`\`
buy: close[t] > high[t-5] and volume[t] > volume[t-1] * 1.5
sell: close[t] < MA_5[t]
\`\`\`

### 逻辑运算
\`\`\`
buy: ReturnRate[t] > 0.03 and STD[t] < 0.02
sell: not (ReturnRate[t] < -0.05)
buy: MA_5[t] > MA_10[t] or close[t] > high[t-1]
\`\`\`

## 重要注意事项
1. **变量必须来自前驱节点**：公式中的变量名只能是 Signal 节点的前驱节点输出，不能使用未连接的节点输出
2. **比较运算必须带时间索引**：\`close[t] > 3000\` ✅，\`close > 3000\` ❌（不能直接比较 Vector）
3. **\`not\` 优先级高于比较运算符**：\`not a > b\` 等价于 \`not (a > b)\`
4. **避免 \`not topk(...)\` 作为卖出条件**：应使用 \`bottomk(...)\` 替代
5. **同一股票同时触发买卖**：系统会输出警告并跳过该信号
6. **不允许做空时**：无持仓股票的 SELL 信号会被过滤为 HOLD
7. **已持仓不追加**：已持仓股票的 BUY 信号会被过滤为 HOLD`
}

// === 策略图约束规则 ===

function getFlowConstraints(): string {
  return `# 策略图（Flow）约束规则

## 必需节点（至少各 1 个）
创建策略图时，必须包含以下节点类型：

1. **数据输入节点 (Input)** — 提供行情数据源
   - nodeType: \`input\`
   - 至少 1 个，提供股票池和频率配置

2. **交易信号生成节点 (Signal)** — 根据指标生成买卖信号
   - nodeType: \`signal\`
   - 至少 1 个，包含 buy/sell 公式表达式

3. **投资组合节点 (Portfolio)** — 管理仓位和资金分配
   - nodeType: \`portfolio\`
   - 至少 1 个，负责资金管理和持仓跟踪

4. **执行交易节点 (Execution)** — 执行实际订单
   - nodeType: \`execution\`
   - 至少 1 个，负责下单执行

## 图结构约束
- **数据流向**：Input → (Function/ML) → Signal → Portfolio → Execution
- **连通性**：所有节点必须能从 Input 节点到达（不能有孤立节点）
- **无环**：图不能有循环依赖（系统会执行拓扑排序检测）
- **禁止自环**：节点不能连接到自身

## 典型策略图结构

\`\`\`
[Input 数据输入]
    │
    ▼
[Function 指标计算] ─── (可选，可多个)
    │
    ▼
[Signal 信号生成] ←── 使用 buy/sell 公式
    │
    ▼
[Portfolio 投资组合]
    │
    ▼
[Execution 交易执行]
\`\`\`

## 节点连接规则
- Input 节点的输出只能连接到后续处理节点
- Signal 节点接收上游指标数据，输出买卖信号
- Portfolio 节点接收 Signal 信号，管理仓位
- Execution 节点接收 Portfolio 指令，执行下单

## Edge（连线）结构

每条连线使用 Vue Flow 标准格式：

\`\`\`json
{
  "id": "1-close->2",
  "source": "1",
  "target": "2",
  "sourceHandle": "1-close",
  "targetHandle": "2",
  "type": "default"
}
\`\`\`

**字段含义**：
- \`source\` / \`target\`：节点 ID（字符串，如 \`"1"\`, \`"2"\`）
- \`sourceHandle\`：源节点的输出端点名称
- \`targetHandle\`：目标节点的输入端点名称

**端点命名规则（重要！）**：

1. **Quote Input 节点（数据输入）**：
   - 输出端点格式：\`{节点id}-{字段名}\`
   - 示例：\`"1-close"\`, \`"1-open"\`, \`"1-high"\`, \`"1-low"\`, \`"1-volume"\`
   - 节点 ID 通常为 \`"1"\`

2. **其他所有节点**（Function、Signal、Portfolio、Execution 等）：
   - 输入端点 = 节点 ID（如 \`"2"\`）
   - 输出端点 = 节点 ID（如 \`"2"\`）

**Edge ID 格式**：\`{source}-{sourceHandle}->{target}\`

## 示例策略图 JSON 结构
\`\`\`json
{
  "nodes": [
    {"id": "1", "data": {"nodeType": "input", "params": {...}}},
    {"id": "2", "data": {"nodeType": "signal", "params": {...}}},
    {"id": "3", "data": {"nodeType": "portfolio", "params": {...}}},
    {"id": "4", "data": {"nodeType": "execution", "params": {...}}}
  ],
  "edges": [
    {"id": "1-close->2", "source": "1", "target": "2", "sourceHandle": "1-close", "targetHandle": "2"},
    {"id": "2->3", "source": "2", "target": "3", "sourceHandle": "2", "targetHandle": "3"},
    {"id": "3->4", "source": "3", "target": "4", "sourceHandle": "3", "targetHandle": "4"}
  ]
}
\`\`\``
}

// === 主 Tool ===

export const strategyTool = tool(
  async ({ action, keyword, data, category }) => {
    switch (action) {
      // === 节点查询 ===

      case "list_nodes": {
        const nodes = keyword ? searchNodes(keyword) :
          category ? getAllNodes().filter(n => n.category === category) :
          getAllNodes()

        const lines = nodes.map(n =>
          `- **${n.label}** (id: \`${n.id}\`, type: \`${n.nodeType}\`) — ${n.description}`
        )
        return `可用节点类型（共 ${nodes.length} 个）：\n${lines.join('\n')}`
      }

      case "get_node_info": {
        if (!keyword) return "错误：get_node_info 需要提供 keyword 参数（节点 id 或名称）"
        const n = getNode(keyword) || searchNodes(keyword)[0]
        if (!n) return `未找到节点 "${keyword}"`

        const info = [
          `**${n.label}** (id: \`${n.id}\`, type: \`${n.nodeType}\`)`,
          ``,
          `**描述**: ${n.description}`,
          `**分类**: ${n.category}`,
          `**输入**: ${n.inputs.length ? n.inputs.join(', ') : '无（数据源）'}`,
          `**输出**: ${n.outputs.length ? n.outputs.join(', ') : '无（终端节点）'}`,
          ``,
          `**参数**：`,
          ...n.params.map(p => {
            const opts = p.options
              ? `，可选: ${Array.isArray(p.options) && typeof p.options[0] === 'object' ? p.options.map((o: any) => o.label).join('/') : p.options.join('/')}`
              : ''
            return `  - \`${p.key}\` (${p.label}): ${p.type}，默认值 = ${JSON.stringify(p.default)}${opts}`
          }),
          ``,
          `**示例**：`,
          `\`\`\`json`,
          JSON.stringify(n.example, null, 2),
          `\`\`\``,
        ]

        // 为 Signal 节点添加特殊提示
        if (n.nodeType === "signal" || n.id === "signal") {
          info.push('')
          info.push('💡 此节点使用 Signal 公式语法（buy/sell 表达式）。使用 `action="signal_syntax"` 获取完整语法文档。')
        }

        // 为其他关键节点添加策略图约束提示
        if (["input", "execution", "portfolio", "function", "xgboost", "spread", "protection"].includes(n.nodeType)) {
          info.push('')
          info.push('💡 创建策略图时需遵循节点约束规则。使用 `action="flow_constraints"` 获取完整约束规则。')
        }

        return info.join('\n')
      }

      // === 策略图 CRUD ===

      case "list_strategies": {
        return listStrategies()
      }

      case "get_strategy": {
        if (!keyword) return "错误：get_strategy 需要提供 keyword 参数（策略 id）"
        return await getStrategy(keyword)
      }

      case "create_strategy": {
        if (!data) return "错误：create_strategy 需要提供 data 参数（策略图 JSON）"
        const strategyName = data.graph?.name || data.name || "未命名策略"
        return await createStrategy(strategyName, data)
      }

      case "update_strategy": {
        if (!keyword) return "错误：update_strategy 需要提供 keyword 参数（策略 id）"
        if (!data) return "错误：update_strategy 需要提供 data 参数（新策略图 JSON）"
        return await updateStrategy(keyword, data)
      }

      case "delete_strategy": {
        if (!keyword) return "错误：delete_strategy 需要提供 keyword 参数（策略 id）"
        return await deleteStrategy(keyword)
      }

      // === 语法和约束文档 ===

      case "signal_syntax": {
        return getSignalSyntaxDoc()
      }

      case "flow_constraints": {
        return getFlowConstraints()
      }

      default:
        return `未知 action: ${action}。支持的 action: list_nodes, get_node_info, list_strategies, get_strategy, create_strategy, update_strategy, delete_strategy, signal_syntax, flow_constraints`
    }
  },
  {
    name: "strategy",
    description: "策略管理工具。可查询节点类型信息，以及创建/获取/更新/删除策略图。action='list_nodes' 列出所有可用节点类型；action='get_node_info' 获取单个节点详情（keyword=节点id）；action='list_strategies' 列出已保存策略；action='get_strategy' 获取策略图 JSON（keyword=策略id）；action='create_strategy' 创建新策略图（data=JSON）；action='update_strategy' 更新策略图（keyword=id, data=JSON）；action='delete_strategy' 删除策略图（keyword=id）；action='signal_syntax' 获取 Signal 节点公式语法文档（buy/sell 表达式规则）；action='flow_constraints' 获取策略图约束规则（必需节点、连通性、数据流向）",
    schema: z.object({
      action: z.enum([
        "list_nodes", "get_node_info",
        "list_strategies", "get_strategy", "create_strategy", "update_strategy", "delete_strategy",
        "signal_syntax", "flow_constraints"
      ]).describe("操作类型"),
      keyword: z.string().optional().describe("节点 id/名称（get_node_info），或策略 id（get/update/delete_strategy），或搜索关键词（list_nodes 时可选）"),
      data: z.any().optional().describe("策略图 JSON 数据（create_strategy / update_strategy 时必填）"),
      category: z.string().optional().describe("节点分类过滤（list_nodes 时可选）: input/process/signal/execution/ml/risk/utility"),
    }),
  }
)
