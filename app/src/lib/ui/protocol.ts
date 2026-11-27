/**
 * UI 协议（通用层）
 *
 * 这里只放**与具体视图无关**的契约：一个 UI 产物长什么样（UiSpec）、
 * 一种类型怎么声明（UiTypeDefinition）、数据怎么续命（UiBinding）。
 *
 * 为什么从 charts/ 挪出来：
 *   产物信封（agent/artifacts.ts）依赖 UiSpec，而 UiSpec 跟「图」没有关系 ——
 *   表格、指标卡、进度条都用它。放在 charts/ 里会让依赖方向反过来
 *   （信封 → 图表），后续加一个非图表的 UI 类型就得往 charts 里塞。
 *
 * 分层：
 *   lib/ui/protocol.ts    ← 本文件：通用契约
 *   lib/ui/registry.ts      类型注册表
 *   lib/charts/shapes.ts    图表类数据形状（列式、表格列、指标条目）
 *   lib/charts/schemas.ts   各类型的 zod 约束（= 工具约束）
 *   lib/charts/resolvers.ts 领域数据源（回测/行情/蒙特卡洛）
 *
 * 设计文档：app/docs/ui-protocol.md
 */

import type { Component } from 'vue'
import type { z } from 'zod'

/* ============================================================================
 * 版本
 * ========================================================================== */

/**
 * 协议版本。
 *
 * 产物会落库（IndexedDB 的 artifacts store + localStorage 兜底），
 * 协议加字段/改语义之后，历史对话里的旧产物还会被重新渲染 ——
 * 消费方要先看这个字段决定怎么解释 data。
 * 缺省（旧数据没有该字段）视为 1。
 */
export const UI_SPEC_VERSION = 1

/* ============================================================================
 * 类型登记
 * ========================================================================== */

/** 已知 UI 类型（在此登记可获得 IDE 提示） */
export const UI_TYPE = {
  TimeSeries: 'timeseries',
  Histogram: 'histogram',
  Matrix: 'matrix',
  Table: 'table',
  Metric: 'metric',
  Progress: 'progress',
} as const

export type KnownUiTypeId = (typeof UI_TYPE)[keyof typeof UI_TYPE]

/** 开放字符串：未登记的类型也能通过（由注册表兜底渲染） */
export type UiTypeId = KnownUiTypeId | (string & {})

/* ============================================================================
 * 参数控件
 * ========================================================================== */

/**
 * 参数控件定义。
 *
 * `layer` 决定这个参数改什么：
 * - `view` —— 呈现层，客户端直接重算，不回源
 * - `data` —— 数据层，改了要触发 resolver 回源换 data
 */
export interface UiControlDef {
  /** 参数 key（对应 UiSpec.params 的键） */
  key: string
  /** 显示名 */
  label: string
  kind: 'slider' | 'select' | 'toggle' | 'number'
  layer: 'view' | 'data'
  /** slider / number 的范围 */
  min?: number
  max?: number
  step?: number
  /** slider 的单位后缀 */
  suffix?: string
  /** select 候选项 —— 由工具/resolver 生成，不让 LLM 现场编 */
  options?: Array<{ value: string; label: string }>
  /** 默认值 */
  default?: unknown
  /** 提示文案 */
  hint?: string
}

/* ============================================================================
 * 数据绑定（inbound：服务端 → UI）
 * ========================================================================== */

/** 停止订阅的条件：事件的 field 字段命中 values 之一 */
export interface UiStopCondition {
  field: string
  values: string[]
}

/**
 * 数据绑定：声明「这份数据从哪来、后续怎么更新」。
 *
 * 生命周期约定（重要）：
 *   绑定只在**产物创建时**确定。用户点击后产生的任务，应当由点击动作
 *   另建一个产物（带自己的 binding），而不是回头改已有产物的 binding ——
 *   产物是不可变快照，改它等于自欺。
 *
 * match 的字段必须是「产物创建时已确定」的值。反例：回测的 run_id 由后端在
 * 请求处理中生成，调用方拿不到（POST 是阻塞的），所以只能按 strategy 名匹配。
 *
 * 注：resolver 型绑定暂仍由类型级声明（UiTypeDefinition.resolver），
 * 等 sse 这条跑通、确认实例级覆盖确有必要再迁移，避免同一件事两套写法。
 */
export type UiBinding =
  /** 一次性快照（默认；不写 binding 等价于此） */
  | { kind: 'snapshot' }
  /** 订阅服务端推送；match 里的字段全等才算本产物的事件 */
  | {
      kind: 'sse'
      /** SSE 事件名，如 quote_download / backtest_progress */
      event: string
      /** 事件载荷必须匹配的字段（全等） */
      match?: Record<string, string | number>
      /** 命中即退订（终态） */
      stopOn?: UiStopCondition
      /**
       * 多久没等到终态就判定失败（毫秒）。**必填**。
       *
       * 服务端 job 被丢弃、连接断掉、页面重载，终态事件都不会来 ——
       * 没有它卡片就永远转圈，用户没有任何办法让它停下。
       * 取值要盖住「两次事件之间的正常间隔」，否则会误判。
       */
      timeoutMs: number
      /**
       * 终态兜底查询地址（可选）。
       *
       * 推送是尽力而为：客户端没连上、后端重启、事件发得太早，
       * 终态事件就永远收不到。这时先查这个地址问服务端「这任务到底成没成」，
       * 查到了就照实显示 —— 服务端已经导完 4090 行，卡片不能还写着失败。
       */
      statusUrl?: string
    }
  /** 定时轮询服务端状态接口；轮询到的快照用同一个 reducer 合并 */
  | {
      kind: 'poll'
      /** 状态查询地址（GET），返回 JSON 快照 */
      url: string
      /** 轮询间隔（毫秒），默认 3000 */
      intervalMs?: number
      /** 多久没拿到终态就判失败（毫秒），必填 */
      timeoutMs: number
    }

/**
 * 状态快照 → 事件载荷。
 *
 * 快照把最近一次进度事件放在 progress 子对象里（{ status, progress: {...} }），
 * reducer 吃的是扁平载荷，这里把 progress 摊平到顶层。
 */
export function normalizeStatusPayload(
  payload: Record<string, unknown>,
): Record<string, unknown> {
  const progress = payload.progress
  if (progress && typeof progress === 'object') {
    return { ...payload, ...(progress as Record<string, unknown>) }
  }
  return payload
}

/** 判断事件载荷是否属于某个绑定 */
export function matchesBinding(binding: UiBinding, event: Record<string, unknown>): boolean {
  if (binding.kind !== 'sse') return false
  const match = binding.match
  if (!match) return true
  return Object.entries(match).every(([key, value]) => String(event[key] ?? '') === String(value))
}

/** 是否已到达终态 */
export function isTerminalEvent(binding: UiBinding, event: Record<string, unknown>): boolean {
  if (binding.kind !== 'sse' || !binding.stopOn) return false
  const raw = event[binding.stopOn.field]
  return raw !== undefined && binding.stopOn.values.includes(String(raw))
}

/**
 * 实时事件 → data 的合并函数。
 *
 * 挂在**类型定义**上而不是绑定上：「这批事件怎么并进这份数据」是类型的语义，
 * 换个实例也还是同一种并法；实例只负责说「订阅哪个事件、匹配什么」。
 * 必须是纯函数 —— 重挂载时可能回放同一条事件。
 */
export type UiReducer = (
  data: Record<string, unknown>,
  event: Record<string, unknown>,
) => Record<string, unknown>

/* ============================================================================
 * 交互绑定（outbound：UI → 参数/动作）
 * ========================================================================== */

/**
 * 手势 → 参数/动作 的映射。
 *
 * 视图组件只 `emit(name, payload)`，不碰 params；宿主查这张表决定落到哪。
 * 表里没有的手势只在视图内部生效（例如纯视觉的缩放），不外传。
 */
export type UiInteractionTarget =
  /** 写入 params[set]；from 指定取事件载荷的哪个字段（缺省 = 整个载荷） */
  | { set: string; from?: string }
  /** 触发产物里已有的 action（暂留接口，v1 不实现） */
  | { action: string }

export type UiInteractions = Record<string, UiInteractionTarget>

/* ============================================================================
 * UI 规格
 * ========================================================================== */

/**
 * UI 规格 = `ui` 产物的 payload。
 *
 * ```json
 * {
 *   "version": 1,
 *   "type": "timeseries",
 *   "title": "策略累计收益 vs 基准",
 *   "source": "backtest:v3",
 *   "params": { "y_format": "percent", "benchmark": "SH000300" },
 *   "data": { "x": [1704067200], "cols": [{ "n": "策略收益", "v": [0.012] }] }
 * }
 * ```
 */
export interface UiSpec {
  /** 协议版本；缺省视为 1（历史产物没有该字段） */
  version?: number
  /** UI 类型 id —— 注册表按它分发到具体视图 */
  type: UiTypeId
  /** 标题 */
  title: string
  /** 副标题 */
  subtitle?: string
  /** 数据来源说明 */
  source?: string
  /**
   * 参数袋：呈现层 + 数据层混装。
   * 各参数的归属由该 UI 类型的 `controls` 声明，渲染层据此决定是否回源。
   */
  params: Record<string, unknown>
  /** 列式数据；精确形状由该 UI 类型的 schema 定义 */
  data: Record<string, unknown>
  /**
   * 初始控件定义 —— 由工具生成（跑一次 resolver 拿到候选列表）。
   * 给了就用它覆盖类型的静态 controls；没给就用静态定义。
   */
  controls?: UiControlDef[]
  /** 数据怎么续命；不写 = 一次性快照 */
  binding?: UiBinding
  /** 手势 → 参数/动作；不写 = 手势只在视图内部生效 */
  interactions?: UiInteractions
}

/* ============================================================================
 * 类型定义
 * ========================================================================== */

/** 数据来源：LLM 直接给 / 系统 resolver 取 / 服务端流式推 */
export type UiIntake = 'llm' | 'resolver' | 'stream'

/**
 * 一个 UI 类型的完整声明。
 *
 * `schema` 是**工具约束**：ui 工具用它校验 LLM 给的 input，
 * 渲染层也用同一份 schema 自检。两边共用一份，描述与实现不会漂移。
 */
export interface UiTypeDefinition {
  id: UiTypeId
  /** 显示名 */
  label: string
  icon?: string
  /** 输入约束（zod schema） */
  schema: z.ZodTypeAny
  /** 给 LLM 看的数据形状说明 —— 工具描述由此自动生成 */
  schemaDescription: string
  /** 参数控件定义 */
  controls: UiControlDef[]
  /**
   * 渲染组件。
   *
   * 契约（schema / controls / resolver）与组件分开注册：
   * 工具层只需要契约，不需要把 .vue 拉进 LLM 调用链；
   * 组件由 UI 层在注册时补上。
   */
  component?: Component | null
  /** 数据层 resolver id —— 数据层控件变更时回源 */
  resolver?: string
  /** 默认参数 */
  defaults?: Record<string, unknown>
  /** 是否支持紧凑形态（对接双密度） */
  compact?: boolean
  /** 是否支持导出 */
  exportable?: boolean
  /**
   * 不对外暴露给 LLM。
   *
   * 用于 progress 这类「只能由工具/动作产生」的类型：它们出现在 ui 工具的
   * builder 列表里只会诱导模型伪造订阅。
   */
  hidden?: boolean
  /** 数据来源（builder 列表里展示，也用于校验 input 的语义） */
  intake?: UiIntake
  /** 可发出的手势（视图组件必须实现同名 emit） */
  gestures?: Array<'click' | 'wheel' | 'drag' | 'brush' | 'zoom'>
  /** 实时事件的合并语义（配合 UiSpec.binding 使用） */
  reducer?: UiReducer
}

/* ============================================================================
 * Resolver
 * ========================================================================== */

/** resolver 的产出 */
export interface ResolverOutput {
  /** 新的数据（替换 UiSpec.data） */
  data: Record<string, unknown>
  /** 可回传新的控件定义 —— 例如把标的列表填进 select 候选项 */
  controls?: UiControlDef[]
}

/**
 * 数据层 resolver。
 *
 * resolver 是**代码**，不是 LLM 编的 JSON —— 与「动作参数由工具生成」同一原则。
 * LLM 只影响 `params`，实际取数逻辑固定在这里。
 */
export type UiResolver = (args: Record<string, unknown>) => Promise<ResolverOutput>
