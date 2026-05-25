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
function validateStrategyGraph(data: any): { valid: boolean; errors: string[]; suggestions: string[] } {
  const errors: string[] = []
  const suggestions: string[] = []

  if (!data || typeof data !== "object") {
    return {
      valid: false,
      errors: ["策略图数据为空或格式错误"],
      suggestions: ["确保 data 参数是有效的 JSON 对象，包含 nodes 和 edges 数组"]
    }
  }

  if (!data.nodes || !Array.isArray(data.nodes) || data.nodes.length === 0) {
    errors.push("缺少 nodes 数组或 nodes 为空")
    suggestions.push("策略图必须包含 nodes 数组，至少需要 4 个节点：Input、Signal、Portfolio、Execution")
  } else {
    // 检查每个节点的结构
    data.nodes.forEach((node: any, index: number) => {
      if (!node.id) {
        errors.push(`节点[${index}] 缺少 id`)
        suggestions.push(`每个节点必须有唯一的字符串 id，如 "${index + 1}"`)
      }
      if (!node.type) {
        errors.push(`节点[${index}] 缺少 type 字段`)
        suggestions.push('每个节点必须有 type: "custom" 字段')
      }
      if (!node.data?.nodeType) {
        errors.push(`节点[${index}] 缺少 data.nodeType`)
        suggestions.push("每个节点必须有 data.nodeType，可选值: input, signal, portfolio, execution, function, xgboost, protection 等")
      }
      if (node.data && !node.data.label) {
        errors.push(`节点[${index}] 缺少 data.label`)
        suggestions.push('每个节点必须有 data.label，如 "数据输入"、"交易信号生成" 等')
      }
      if (!node.position) {
        errors.push(`节点[${index}] 缺少 position`)
        suggestions.push("每个节点必须有 position: { x: 数字, y: 数字 }，建议按流向从左到右排列")
      }
    })

    // 检查必需节点类型
    const nodeTypes = data.nodes.map((n: any) => n.data?.nodeType)
    if (!nodeTypes.includes("input")) {
      errors.push("缺少必需节点类型: input (数据输入)")
      suggestions.push('添加: { id: "1", type: "custom", data: { label: "数据输入", nodeType: "input", params: { source: "股票", code: ["sz.000001"], freq: "1d" } }, position: { x: 100, y: 200 } }')
    }
    if (!nodeTypes.includes("signal")) {
      errors.push("缺少必需节点类型: signal (信号生成)")
      suggestions.push('添加: { id: "2", type: "custom", data: { label: "交易信号生成", nodeType: "signal", params: { buy: "close[t] > MA_5[t]", sell: "close[t] < MA_5[t]" } }, position: { x: 400, y: 200 } }')
    }
    if (!nodeTypes.includes("portfolio")) {
      errors.push("缺少必需节点类型: portfolio (投资组合)")
      suggestions.push('添加: { id: "3", type: "custom", data: { label: "投资组合", nodeType: "portfolio", params: { positionRatio: 1.0 } }, position: { x: 700, y: 200 } }')
    }
    if (!nodeTypes.includes("execution")) {
      errors.push("缺少必需节点类型: execution (交易执行)")
      suggestions.push('添加: { id: "4", type: "custom", data: { label: "执行交易", nodeType: "execution", params: { commission: 0.0003, stampDuty: 0.001 } }, position: { x: 1000, y: 200 } }')
    }
  }

  if (!data.edges || !Array.isArray(data.edges)) {
    errors.push("缺少 edges 数组")
    suggestions.push("策略图必须包含 edges 数组，定义节点之间的连接关系")
  } else {
    const nodeIds = new Set(data.nodes?.map((n: any) => n.id) || [])

    data.edges.forEach((edge: any, index: number) => {
      if (!edge.source) {
        errors.push(`边[${index}] 缺少 source`)
        suggestions.push("每条边必须有 source 字段（源节点 ID）")
      }
      if (!edge.target) {
        errors.push(`边[${index}] 缺少 target`)
        suggestions.push("每条边必须有 target 字段（目标节点 ID）")
      }
      if (!edge.sourceHandle) {
        errors.push(`边[${index}] 缺少 sourceHandle`)
        suggestions.push("【关键】Input 节点的 sourceHandle 格式为 \"{id}-{字段名}\"（如 \"1-close\"），其他节点直接用节点 ID（如 \"2\"）")
      }
      if (!edge.targetHandle) {
        errors.push(`边[${index}] 缺少 targetHandle`)
        suggestions.push("targetHandle 通常等于目标节点的 ID（如 \"2\", \"3\"）")
      }

      if (edge.source && !nodeIds.has(edge.source)) {
        errors.push(`边[${index}] 的 source "${edge.source}" 指向不存在的节点`)
        suggestions.push("检查 source 值是否与实际节点 ID 匹配")
      }
      if (edge.target && !nodeIds.has(edge.target)) {
        errors.push(`边[${index}] 的 target "${edge.target}" 指向不存在的节点`)
        suggestions.push("检查 target 值是否与实际节点 ID 匹配")
      }
    })
  }

  return { valid: errors.length === 0, errors, suggestions }
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
      `策略图 "${strategy.name}" (${id}) 更新失败：结构验证未通过`,
      ``,
      `错误信息：`,
      ...validation.errors.map(e => `- ${e}`),
      ``,
      `修正建议：`,
      ...validation.suggestions.map(s => `- ${s}`)
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
    `策略图结构：`,
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
      `策略图 "${name}" 创建失败：结构验证未通过`,
      ``,
      `错误信息：`,
      ...validation.errors.map(e => `- ${e}`),
      ``,
      `修正建议：`,
      ...validation.suggestions.map(s => `- ${s}`)
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
    `策略图结构：`,
    `\`\`\`json`,
    JSON.stringify(flowData, null, 2),
    `\`\`\``
  ].join('\n')
}

// === Signal 公式语法文档 ===

function getSignalSyntaxDoc(): string {
  return `# Signal 公式语法

Signal 节点使用类 C 表达式语法生成买卖信号，公式分为 \`buy\` 和 \`sell\` 两部分。

## 变量来源（核心规则）

**公式中的变量名只能来自 Signal 节点的前驱节点（直接相连的上游节点）的输出。**

### 如何确定可用变量

1. 查看 Signal 节点的上游节点是谁（通过 edges 的 target 指向 Signal 节点）
2. 使用 \`strategy(action="get_node_info", keyword="上游节点类型")\` 查看该节点的 outputs 字段
3. 这些 outputs 就是公式中可以使用的变量名

### 常见前驱节点及其输出

| 前驱节点类型 | 可用变量 | 示例 |
|-------------|---------|------|
| Input（数据输入） | close, open, high, low, volume | \`close[t]\`, \`volume[t]\` |
| Function（指标计算） | 根据方法不同而不同 | MA: \`MA_5[t]\`, STD: \`STD_20[t]\`, Return: \`ReturnRate[t]\` |
| ML 模型 | prediction | \`prediction[t]\` |

### 变量引用格式

格式：\`变量名[时间索引]\`
- \`close[t]\` — 当前时刻收盘价
- \`MA_5[t-1]\` — 前 1 个时刻的 5 日均线
- \`ReturnRate[t-5]\` — 前 5 个时刻的收益率

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
2. **比较运算必须带时间索引**：\`close[t] > 3000\` 正确，\`close > 3000\` 错误（不能直接比较 Vector）
3. \`not\` 优先级高于比较运算符：\`not a > b\` 等价于 \`not (a > b)\`
4. 避免 \`not topk(...)\` 作为卖出条件：应使用 \`bottomk(...)\` 替代
5. 同一股票同时触发买卖：系统会输出警告并跳过该信号
6. 不允许做空时：无持仓股票的 SELL 信号会被过滤为 HOLD
7. 已持仓不追加：已持仓股票的 BUY 信号会被过滤为 HOLD`
}

// === HMM 市场状态识别使用指南 ===

function getHMMUsageGuide(): string {
  return `# HMM 市场状态识别使用指南

## 何时使用
- 需要识别市场状态（牛市/熊市/震荡）并据此调整策略行为
- 需要根据状态转移概率预测未来市场走向
- 需要在不同市场状态下使用不同的交易逻辑（如牛市追涨、熊市防守）

## 典型策略流
\`\`\`
[Input 数据输入] → [HMM 市场状态识别] → [Signal 交易信号] → [Portfolio] → [Execution]
                        ↓
                  输出 4 个变量到 context
\`\`\`

## 输出变量说明（Signal 公式中可用）

| 变量名 | 类型 | 说明 |
|--------|------|------|
| \`hmm_state\` | 整数时间序列 | 当前最可能的隐状态编号（0, 1, 2...），如 n_states=3 则值为 0/1/2 |
| \`hmm_probs\` | 向量时间序列 | 各状态的概率分布，长度为 n_states，如 [0.7, 0.2, 0.1] 表示 70% 概率为状态 0 |
| \`hmm_transition\` | 向量时间序列 | 状态转移矩阵展平，长度为 n_states²，按行优先排列：A[0,0], A[0,1], ..., A[N-1,N-1] |
| \`hmm_duration\` | 向量时间序列 | 各状态的期望持续时间（天），长度为 n_states |

## 训练机制
- **离线批量训练**：每 \`retrain_interval\` 天用过去 \`train_window\` 天数据重新训练
- **预热期**：前 \`warmup_period\` 天不输出状态（模型未训练）
- **滑动窗口**：缓冲区满后每次左移一位追加新观测

## Signal 公式中的用法

HMM 输出的是**时间序列**，在 Signal 公式中需带时间索引 \`[t]\` 引用：

\`\`\`
// 基于状态编号的简单规则
buy: hmm_state[t] == 0    // 状态 0 时买入（假设 0 = 牛市）
sell: hmm_state[t] == 2   // 状态 2 时卖出（假设 2 = 熊市）

// 基于状态概率的高置信度规则
buy: hmm_state[t] == 0 and hmm_probs[t] > 0.7   // 高置信度牛市

// 结合技术指标的状态过滤
buy: close[t] > MA_5[t] and hmm_state[t] == 0   // 金叉 + 牛市状态
sell: close[t] < MA_5[t] and hmm_state[t] == 2  // 死叉 + 熊市状态

// 基于状态转移的预测（转移矩阵展平后索引）
// 例：n_states=3 时，A[0,1] 在 hmm_transition[1]，A[1,2] 在 hmm_transition[5]
\`\`\`

## 参数建议
- **n_states**：一般 2-3 即可
  - 2 状态：牛/熊
  - 3 状态：牛/震荡/熊（推荐）
- **train_window**：252（一年交易日）
- **retrain_interval**：60（约一季度重训一次）
- **warmup_period**：60（给模型足够的预热时间）
- **features**：建议包含 close 和一个波动率指标，如 \`close,volatility\`

## 注意事项
1. 状态编号（0, 1, 2）的具体含义由模型训练决定，**不保证** 0=牛市、1=震荡、2=熊市
2. 建议先用简单策略测试，观察输出状态与实际市场状态的对应关系
3. 预热期内 HMM 节点返回 Skip，下游 Signal 节点收不到数据会跳过处理
4. HMM 节点必须在 Input 节点之后，Signal 节点之前`
}

// === EMD 分解使用指南 ===

function getEMDUsageGuide(): string {
  return `# EMD 分解使用指南

## 何时使用
- 需要将非平稳时间序列（如价格、收益率）分解为多个本征模态函数（IMF）
- 需要降噪或多尺度分析（如分离高频噪声和低频趋势）
- 需要提取不同时间尺度的特征供 Signal 节点使用

## 典型策略流
\`\`\`
[Input 数据输入] → [EMD 分解] → [Signal 交易信号] → [Portfolio] → [Execution]
                        ↓
                  输出多个 IMF 分量到 context
\`\`\`

## 输出变量说明（Signal 公式中可用）

EMD 节点对每个输入时间序列输出 \`numIMFs\` 个 IMF 分量，命名规则为：

| 变量名 | 说明 |
|--------|------|
| \`{label}.IMF_0\` | 第 0 个 IMF 分量（最高频，波动最大） |
| \`{label}.IMF_1\` | 第 1 个 IMF 分量 |
| \`{label}.IMF_2\` | 第 2 个 IMF 分量 |
| \`...\` | ... |
| \`{label}.IMF_{N-1}\` | 第 N-1 个 IMF 分量（最低频，接近趋势） |

- \`label\` 是 EMD 节点在策略图中的标签名（可在节点参数中修改）
- 分量编号从 0 开始，\`IMF_0\` 频率最高，编号越大频率越低
- 所有输出均为**双精度浮点时间序列**（Double_TimeSeries）

## Signal 公式中的用法

EMD 输出的是**时间序列**，在 Signal 公式中需带时间索引 \`[t]\` 引用：

\`\`\`
// 使用最高频分量作为动量指标
buy: node1.IMF_0[t] > 0.5
sell: node1.IMF_0[t] < -0.5

// 使用低频分量判断趋势方向
buy: node1.IMF_3[t] > 0  // 低频分量为正，上升趋势
sell: node1.IMF_3[t] < 0 // 低频分量为负，下降趋势

// 组合多个分量
buy: node1.IMF_0[t] > 0 and node1.IMF_2[t] > 0  // 高频和低频同向
sell: node1.IMF_0[t] < node1.IMF_1[t]  // 高频低于中频，短期走弱

// 与原始价格结合使用
buy: close[t] > MA_5[t] and node1.IMF_0[t] > 0  // 金叉 + 高频动量为正
\`\`\`

## 参数建议
- **numIMFs**：一般 3~8 即可
  - 3~5：适合短期交易，保留主要波动特征
  - 5~8：适合多尺度分析，分离更精细
  - 过大（>10）会导致计算缓慢且分量物理意义不清晰
- **输入数据**：建议使用收益率而非原始价格，或先做标准化

## 注意事项
1. IMF 分量的物理意义由数据本身决定，不保证 IMF_0 一定是噪声或趋势
2. EMD 分解计算量较大，建议在前置节点做好数据过滤（如只选关注的股票）
3. EMD 节点必须在 Input 节点之后，Signal 节点之前
4. 分解后的 IMF 分量数量等于输入的 numIMFs 参数，Signal 公式中可用的变量名由节点的 label 决定`
}

// === 策略图约束规则 ===

function getFlowConstraints(): string {
  return `# 策略图（Flow）约束规则

## 节点结构

每个节点必须包含以下字段：

\`\`\`json
{
  "id": "1",
  "type": "custom",
  "data": {
    "label": "数据输入",
    "nodeType": "input",
    "params": { ... }
  },
  "position": { "x": 100, "y": 200 }
}
\`\`\`

**字段说明**：
- \`id\`：字符串，唯一标识，建议用数字字符串如 "1", "2", "3"
- \`type\`：固定为 \`"custom"\`，不可修改
- \`data.label\`：节点显示名称，需与 nodeType 匹配（见下表）
- \`data.nodeType\`：节点类型，决定节点行为
- \`data.params\`：节点参数，根据 nodeType 不同而不同
- \`position\`：布局坐标，建议从左到右排列（x 递增）

## 必需节点

创建策略图时，必须包含以下 4 类节点：

| nodeType | data.label | 必填参数 | 示例 |
|----------|------------|----------|------|
| \`input\` | "数据输入" | source, code, freq | \`{ source: "股票", code: ["sz.000001"], freq: "1d" }\` |
| \`signal\` | "交易信号生成" | buy, sell | \`{ buy: "close[t] > MA_5[t]", sell: "close[t] < MA_5[t]" }\` |
| \`portfolio\` | "投资组合" | positionRatio | \`{ positionRatio: 1.0 }\` |
| \`execution\` | "执行交易" | commission, stampDuty | \`{ commission: 0.0003, stampDuty: 0.001 }\` |

## Edge（连线）结构

\`\`\`json
{
  "id": "1-close->2",
  "source": "1",
  "target": "2",
  "sourceHandle": "1-close",
  "targetHandle": "2",
  "type": "default",
  "markerEnd": { "type": "arrowclosed" },
  "style": { "stroke": "#666" }
}
\`\`\`

**端点命名规则（极易出错）**：

1. **Input 节点的输出端点**：格式为 \`{节点id}-{字段名}\`
   - 示例：\`"1-close"\`, \`"1-open"\`, \`"1-high"\`, \`"1-low"\`, \`"1-volume"\`

2. **其他所有节点的输入/输出端点**：直接使用节点 ID
   - 示例：\`"2"\`, \`"3"\`, \`"4"\`

**Edge ID 格式**：\`{source}-{sourceHandle}->{target}\`

## 数据流向

\`\`\`
[Input] → [Function/ML 可选] → [Signal] → [Portfolio] → [Execution]
\`\`\`

## 完整示例（均线交叉策略）

\`\`\`json
{
  "nodes": [
    {
      "id": "1",
      "type": "custom",
      "data": { "label": "数据输入", "nodeType": "input", "params": { "source": "股票", "code": ["sz.000001"], "freq": "1d" } },
      "position": { "x": 100, "y": 200 }
    },
    {
      "id": "2",
      "type": "custom",
      "data": { "label": "交易信号生成", "nodeType": "signal", "params": { "buy": "MA_5[t] >= MA_15[t]", "sell": "MA_5[t] < MA_15[t]" } },
      "position": { "x": 400, "y": 200 }
    },
    {
      "id": "3",
      "type": "custom",
      "data": { "label": "投资组合", "nodeType": "portfolio", "params": { "positionRatio": 1.0 } },
      "position": { "x": 700, "y": 200 }
    },
    {
      "id": "4",
      "type": "custom",
      "data": { "label": "执行交易", "nodeType": "execution", "params": { "commission": 0.0003, "stampDuty": 0.001 } },
      "position": { "x": 1000, "y": 200 }
    }
  ],
  "edges": [
    { "id": "1-close->2", "source": "1", "target": "2", "sourceHandle": "1-close", "targetHandle": "2", "type": "default", "markerEnd": { "type": "arrowclosed" }, "style": { "stroke": "#666" } },
    { "id": "2->3", "source": "2", "target": "3", "sourceHandle": "2", "targetHandle": "3", "type": "default", "markerEnd": { "type": "arrowclosed" }, "style": { "stroke": "#666" } },
    { "id": "3->4", "source": "3", "target": "4", "sourceHandle": "3", "targetHandle": "4", "type": "default", "markerEnd": { "type": "arrowclosed" }, "style": { "stroke": "#666" } }
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
          info.push('[提示] 此节点使用 Signal 公式语法（buy/sell 表达式）。使用 `action="signal_syntax"` 获取完整语法文档。')
        }

        // 为 HMM 节点添加特殊提示
        if (n.nodeType === "hmm" || n.id === "hmm") {
          info.push('')
          info.push('[提示] HMM 市场状态识别节点有详细的使用指南。使用 `action="hmm_usage"` 获取完整说明（输出变量含义、Signal 公式用法、参数建议）。')
        }

        // 为 EMD 节点添加特殊提示
        if (n.nodeType === "emd" || n.id === "emd") {
          info.push('')
          info.push('[提示] EMD 分解节点有详细的使用指南。使用 `action="emd_usage"` 获取完整说明（输出变量命名规则、Signal 公式用法、参数建议）。')
        }

        // 为其他关键节点添加策略图约束提示
        if (["input", "execution", "portfolio", "function", "xgboost", "spread", "protection"].includes(n.nodeType)) {
          info.push('')
          info.push('[提示] 创建策略图时需遵循节点约束规则。使用 `action="flow_constraints"` 获取完整约束规则。')
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

      case "hmm_usage": {
        return getHMMUsageGuide()
      }

      case "emd_usage": {
        return getEMDUsageGuide()
      }

      default:
        return `未知 action: ${action}。支持的 action: list_nodes, get_node_info, list_strategies, get_strategy, create_strategy, update_strategy, delete_strategy, signal_syntax, flow_constraints, hmm_usage, emd_usage`
    }
  },
  {
    name: "strategy",
    description: "策略管理工具。查询节点类型、创建/管理策略图、获取 Signal 公式语法和策略图约束规则。\n\n创建策略图关键约束：\n- 必须包含 4 类节点：input, signal, portfolio, execution\n- 每个节点必须有：id, type=\"custom\", data.label, data.nodeType, data.params, position\n- Edge 端点：Input 节点输出为 \"{id}-{字段名}\"（如 \"1-close\"），其他节点直接用 ID（如 \"2\"）\n- 数据流向：Input → Function/ML → Signal → Portfolio → Execution\n- 可选节点：HMM（市场状态识别，放在 Input 和 Signal 之间）、EMD（信号分解）\n\naction 说明：\n- list_nodes: 列出所有可用节点类型\n- get_node_info: 获取单个节点详情（keyword=节点 id 或名称）\n- list_strategies: 列出已保存策略\n- get_strategy: 获取策略图 JSON（keyword=策略 id）\n- create_strategy: 创建新策略图（data=JSON，必须包含 nodes 和 edges）\n- update_strategy: 更新策略图（keyword=id, data=JSON）\n- delete_strategy: 删除策略图（keyword=id）\n- signal_syntax: 获取 Signal 公式语法（buy/sell 表达式规则）\n- flow_constraints: 获取完整策略图约束规则（节点结构、Edge 端点命名、完整示例）\n- hmm_usage: 获取 HMM 市场状态识别使用指南（输出变量、Signal 公式用法、参数建议）\n- emd_usage: 获取 EMD 分解使用指南（输出变量命名规则、Signal 公式用法、参数建议）",
    schema: z.object({
      action: z.enum([
        "list_nodes", "get_node_info",
        "list_strategies", "get_strategy", "create_strategy", "update_strategy", "delete_strategy",
        "signal_syntax", "flow_constraints", "hmm_usage", "emd_usage"
      ]).describe("操作类型"),
      keyword: z.string().optional().describe("节点 id/名称（get_node_info），或策略 id（get/update/delete_strategy），或搜索关键词（list_nodes 时可选）"),
      data: z.any().optional().describe("策略图 JSON 数据（create_strategy / update_strategy 时必填）"),
      category: z.string().optional().describe("节点分类过滤（list_nodes 时可选）: input/process/signal/execution/ml/risk/utility"),
    }),
  }
)
