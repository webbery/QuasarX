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

import type { UiSpec } from '@/lib/ui/protocol'

/* ============================================================================
 * 类型登记
 * ========================================================================== */

/** 已知产物类型（在此登记可获得 IDE 提示） */
export const ArtifactKind = {
  Patch: 'patch',
  Action: 'action',
  /** 通用 UI 产物：payload 是一份平铺的 UiSpec，由 UI 注册表分发渲染 */
  Ui: 'ui',
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
  /** 紧凑态的渲染高度（px）；不给则用渲染器默认值 */
  compactHeight?: number
  /** 是否支持紧凑 ↔ 完整双密度 */
  collapsible?: boolean
  /** 默认是否折叠 */
  defaultCollapsed?: boolean
}

/** 产物呈现密度 */
export type ArtifactDensity = 'compact' | 'full'

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
  | 'apply_patch'
  | 'open_panel'

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
 * 具体产物类型
 * ========================================================================== */

export interface PatchArtifact extends ArtifactEnvelope<PatchPayload> {
  kind: 'patch'
}
export interface ActionArtifact extends ArtifactEnvelope<ActionPayload> {
  kind: 'action'
}

/**
 * 通用 UI 产物。
 *
 * payload 是一份平铺的 UiSpec —— 「画什么、怎么画」由 spec.type 决定，
 * 数据在 spec.data（列式），参数在 spec.params。
 */
export interface UiArtifact extends ArtifactEnvelope<UiSpec> {
  kind: 'ui'
}

export type AgentArtifact = PatchArtifact | ActionArtifact | UiArtifact

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
