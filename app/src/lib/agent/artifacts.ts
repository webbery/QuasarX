/**
 * Agent 产物（Artifact）契约
 *
 * 设计要点：
 * 1. **信封稳定、载荷开放** —— ArtifactEnvelope 是所有产物共用的外壳，payload
 *    的结构由各 kind 自己的契约决定。新增产物类型不需要改本文件。
 * 2. **kind 是开放字符串** —— 未登记的类型也能通过，由 ArtifactRegistry 兜底渲染，
 *    不会因为 Agent 吐出未知产物而让对话流崩掉。
 * 3. **产物不携带呈现细节** —— 不含 ECharts option、颜色、坐标轴样式。
 *    展示归渲染器管，换主题或加图表类型不用动数据契约。
 * 4. **产物不可变** —— 重新生成时新建一个 id，而不是改旧产物。
 * 5. **LLM 不生产产物** —— 产物由工具层产出，LLM 只拿到一段文本摘要。
 */

import type { TimeSeries, HistogramData } from '@/lib/chartData'

/* ============================================================================
 * 类型登记
 * ========================================================================== */

/** 已知产物类型（在此登记可获得 IDE 提示） */
export const ArtifactKind = {
  Chart: 'chart',
  Metric: 'metric',
  Table: 'table',
  Patch: 'patch',
  Action: 'action',
  Error: 'error',
} as const

export type KnownArtifactKind = (typeof ArtifactKind)[keyof typeof ArtifactKind]

/** 开放字符串：未登记的类型也能通过（由注册表兜底渲染） */
export type ArtifactKind = KnownArtifactKind | (string & {})

/* ============================================================================
 * 信封
 * ========================================================================== */

/** UI 呈现提示 */
export interface ArtifactUiHints {
  /** 建议渲染高度（px） */
  height?: number
  /** 是否支持紧凑 ↔ 完整双密度 */
  collapsible?: boolean
  /** 默认是否折叠 */
  defaultCollapsed?: boolean
  /** 是否可被钉住 */
  pin?: boolean
}

/**
 * 所有产物共用的信封。
 * 载荷结构由各 kind 的契约决定（见下方各 *Payload）。
 */
export interface ArtifactEnvelope<TPayload = unknown> {
  /** 稳定 id，跨重渲染不变 */
  id: string
  kind: ArtifactKind
  title: string
  subtitle?: string
  /** 数据来源说明，UI 显示在卡片底部 */
  source?: string
  createdAt: number
  payload: TPayload
  /** 产物附带的可执行动作 */
  actions?: ArtifactAction[]
  ui?: ArtifactUiHints
}

/* ============================================================================
 * 动作：声明式，由用户点击触发，不由 LLM 直接执行
 * ========================================================================== */

export type ActionKind =
  | 'run_backtest'
  | 'run_strategy'
  | 'stop_strategy'
  | 'deploy_strategy'
  | 'query_data'
  | 'apply_patch'
  | 'open_panel'
  | 'export'
  | 'custom'

export interface ArtifactAction {
  id: string
  kind: ActionKind
  label: string
  icon?: string
  /** 参数由工具生成，不由 LLM 现场编造 */
  params?: Record<string, unknown>
  /** 覆盖默认 kind，用于注册自定义处理器 */
  handler?: string
  /** 危险动作需要二次确认 */
  destructive?: boolean
  confirm?: { title: string; body?: string }
  pendingLabel?: string
  doneLabel?: string
  disabled?: boolean
  disabledReason?: string
}

/* ============================================================================
 * 图表载荷
 * ========================================================================== */

export type ChartType =
  | 'line'
  | 'area'
  | 'bar'
  | 'scatter'
  | 'candlestick'
  | 'histogram'
  | 'heatmap'

/** 图上的标记（竖线 / 横线 / 打点） */
export interface ChartMarker {
  type: 'vline' | 'hline' | 'point'
  /** vline / point：Unix 时间戳（秒），渲染器就近匹配到索引 */
  timestamp?: number
  /** hline / point：数值 */
  value?: number
  /** point：属于哪个序列 */
  seriesIndex?: number
  label?: string
  color?: string
  style?: 'solid' | 'dashed'
}

/**
 * 时刻条目 —— 图上标记的信息化描述。
 * 与 ChartMarker 通过 markerIndex 双向关联：悬停时刻条目高亮图上标记，反之亦然。
 */
export interface ChartMoment {
  id: string
  /** Unix 时间戳（秒） */
  timestamp: number
  title: string
  detail?: string
  severity?: 'info' | 'positive' | 'warning' | 'critical'
  /** 数据来源，如 "mutation.detect" */
  source?: string
  /** 指向 payload.markers 的下标 */
  markerIndex?: number
}

export interface ChartPayload {
  chartType: ChartType
  /** 折线 / 柱状 / 面积 / 散点：X = timestamps，Y = values[][], labels = 每列名 */
  timeSeries?: TimeSeries
  /** 分布直方图 */
  histogram?: HistogramData
  /** 热力图（月度收益、相关性矩阵） */
  matrix?: { rows: string[]; cols: string[]; values: number[][] }
  xAxisLabel?: string
  yAxisLabel?: string
  yFormat?: 'number' | 'percent' | 'currency'
  /** 双轴：指定序列挂独立 Y 轴 */
  secondaryAxis?: {
    seriesIndex: number
    yAxisLabel?: string
    yFormat?: 'number' | 'percent'
  }
  markers?: ChartMarker[]
  /** 时刻列表 */
  moments?: ChartMoment[]
  legend?: { show?: boolean; note?: string }
}

/* ============================================================================
 * 指标载荷
 * ========================================================================== */

export type MetricFormat = 'number' | 'percent' | 'currency' | 'ratio' | 'raw'

export interface MetricItem {
  key: string
  label: string
  value: number | string
  format?: MetricFormat
  /** 小数位；缺省 percent = 1，number = 2 */
  precision?: number
  /** 与基期的变化 */
  delta?: number
  /** 变化说明，如「年化」 */
  deltaLabel?: string
  /** 语义色；不给则按 delta 符号推断 */
  tone?: 'good' | 'bad' | 'neutral'
}

export interface MetricPayload {
  items: MetricItem[]
}

/* ============================================================================
 * 表格载荷
 * ========================================================================== */

export interface TableColumn {
  key: string
  label: string
  align?: 'left' | 'right' | 'center'
  format?: MetricFormat
  precision?: number
  width?: number
}

export interface TablePayload {
  columns: TableColumn[]
  rows: Record<string, unknown>[]
  /** 真实总行数（rows 被截断时） */
  total?: number
  truncated?: boolean
}

/* ============================================================================
 * 策略修改提案载荷
 * ========================================================================== */

export type PatchOpKind = 'update' | 'add' | 'remove' | 'move'

export interface PatchOp {
  op: PatchOpKind
  nodeId: string
  nodeLabel?: string
  /** 参数路径，如 "params.threshold" */
  path: string
  /** 人类可读描述，如「止损阈值」 */
  label: string
  before?: unknown
  after?: unknown
}

export interface PatchPayload {
  /** 一句话说明这次改了什么 */
  summary: string
  target: {
    strategyId: string
    strategyName: string
    versionBefore?: string
  }
  ops: PatchOp[]
  /** 直接复用 strategy.ts 里 validateStrategyGraph 的结果 */
  validation: { valid: boolean; errors: string[]; suggestions: string[] }
  /** Agent 给出的风险提示 */
  riskNote?: string
  /** 应用后的完整策略图；前端可据此预览，也可直接写入 historyStore */
  graph?: { nodes: any[]; edges: any[] }
}

/* ============================================================================
 * 操作载荷
 * ========================================================================== */

export interface ActionPayload {
  /** 上下文说明，如「这会重新跑一轮回测并覆盖当前结果」 */
  context?: string
  actions: ArtifactAction[]
}

/* ============================================================================
 * 错误载荷
 * ========================================================================== */

export interface ErrorPayload {
  code?: string
  message: string
  /** 修复建议 */
  hint?: string
}

/* ============================================================================
 * 具体产物类型
 * ========================================================================== */

export interface ChartArtifact extends ArtifactEnvelope<ChartPayload> {
  kind: 'chart'
}
export interface MetricArtifact extends ArtifactEnvelope<MetricPayload> {
  kind: 'metric'
}
export interface TableArtifact extends ArtifactEnvelope<TablePayload> {
  kind: 'table'
}
export interface PatchArtifact extends ArtifactEnvelope<PatchPayload> {
  kind: 'patch'
}
export interface ActionArtifact extends ArtifactEnvelope<ActionPayload> {
  kind: 'action'
}
export interface ErrorArtifact extends ArtifactEnvelope<ErrorPayload> {
  kind: 'error'
}

export type AgentArtifact =
  | ChartArtifact
  | MetricArtifact
  | TableArtifact
  | PatchArtifact
  | ActionArtifact
  | ErrorArtifact

/* ============================================================================
 * 工具返回的双通道
 * ========================================================================== */

/**
 * 工具返回值。
 * - `text` 回灌给 LLM（它看到的是一段文字摘要）
 * - `artifact` 走事件通道进 UI（LLM 完全不接触）
 */
export interface ToolArtifactResult {
  text: string
  artifact?: ArtifactEnvelope
}

/* ============================================================================
 * 构造函数
 * ========================================================================== */

let artifactSeq = 0

export function nextArtifactId(prefix = 'art'): string {
  artifactSeq += 1
  return `${prefix}-${Date.now().toString(36)}-${artifactSeq.toString(36)}`
}

export interface MakeArtifactOptions {
  id?: string
  subtitle?: string
  source?: string
  actions?: ArtifactAction[]
  ui?: ArtifactUiHints
}

/** 构造一个产物信封（自动补 id / createdAt） */
export function makeArtifact<TPayload>(
  kind: ArtifactKind,
  title: string,
  payload: TPayload,
  opts: MakeArtifactOptions = {},
): ArtifactEnvelope<TPayload> {
  return {
    id: opts.id ?? nextArtifactId(kind),
    kind,
    title,
    subtitle: opts.subtitle,
    source: opts.source,
    createdAt: Date.now(),
    payload,
    actions: opts.actions,
    ui: opts.ui,
  }
}

/** 判断一个工具返回值是否携带产物 */
export function isToolArtifactResult(value: unknown): value is ToolArtifactResult {
  return (
    !!value &&
    typeof value === 'object' &&
    'text' in (value as Record<string, unknown>) &&
    typeof (value as Record<string, unknown>).text === 'string'
  )
}
