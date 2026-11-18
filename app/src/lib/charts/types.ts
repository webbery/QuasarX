/**
 * UI 规格契约
 *
 * 设计要点：
 * 1. **数据与 UI 分离** —— `UiSpec` 只描述「画什么、怎么画」，
 *    数据本身以列式（columnar）形状放在 `data` 里，与呈现细节解耦。
 * 2. **平铺** —— 信封只有 5 个标量字段 + `params` / `data` 两个袋子，深度 ≤2。
 * 3. **名字绑在值上** —— 列式 `cols: [{ n, v }]` 取代 `values[][] + labels`，
 *    下标错位从根上消失，还能顺带带单位与语义色。
 * 4. **约束在代码里** —— 每个 UI 类型声明自己的 zod schema，
 *    工具用它校验输入（工具约束），渲染层用同一份 schema 自检。
 */

import type { Component } from 'vue'
import type { z } from 'zod'

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
} as const

export type KnownUiTypeId = (typeof UI_TYPE)[keyof typeof UI_TYPE]

/** 开放字符串：未登记的类型也能通过（由注册表兜底渲染） */
export type UiTypeId = KnownUiTypeId | (string & {})

/* ============================================================================
 * 数据形状
 * ========================================================================== */

/**
 * 列式数据的一列：名字 + 值数组。
 * 名字与值绑定，避免 `values[i]` 与 `labels[i]` 下标错位。
 */
export interface UiColumn {
  /** 列名（序列名 / 指标名） */
  n: string
  /** 值数组；允许 null（表示该点缺数据） */
  v: Array<number | string | null>
  /** 单位后缀，如 '%' / '¥' */
  u?: string
  /** 语义色；不给则按值符号推断 */
  tone?: 'good' | 'bad' | 'neutral'
}

/** 表格列定义 */
export interface UiTableColumn {
  /** 列 key（对应 row 里的字段名） */
  k: string
  /** 显示名 */
  label: string
  /** 数值格式 */
  fmt?: 'number' | 'percent' | 'currency' | 'ratio' | 'raw'
  /** 小数位 */
  precision?: number
  /** 对齐 */
  align?: 'left' | 'right' | 'center'
  /** 列宽 */
  width?: number
}

/** 指标条目 */
export interface UiMetricItem {
  key: string
  label: string
  value: number | string
  format?: 'number' | 'percent' | 'currency' | 'ratio' | 'raw'
  precision?: number
  delta?: number
  deltaLabel?: string
  tone?: 'good' | 'bad' | 'neutral'
}

/** 时刻条目 —— 图上标记的信息化描述，渲染在图下方的时刻条 */
export interface UiMoment {
  id: string
  /** Unix 时间戳（秒） */
  timestamp: number
  title: string
  detail?: string
  severity?: 'info' | 'positive' | 'warning' | 'critical'
}

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
 * UI 规格
 * ========================================================================== */

/**
 * UI 规格 = `ui` 产物的 payload。
 *
 * ```json
 * {
 *   "type": "timeseries",
 *   "title": "策略累计收益 vs 基准",
 *   "source": "backtest:v3",
 *   "params": { "y_format": "percent", "benchmark": "SH000300" },
 *   "data": {
 *     "x": [1704067200, 1704153600],
 *     "cols": [
 *       { "n": "策略收益", "v": [0.012, 0.031], "u": "%" },
 *       { "n": "基准收益", "v": [0.004, 0.009], "u": "%" }
 *     ]
 *   }
 * }
 * ```
 */
export interface UiSpec {
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
}

/* ============================================================================
 * 注册表
 * ========================================================================== */

/**
 * 一个 UI 类型的完整声明。
 *
 * `schema` 是**工具约束**：visualize 工具用它校验 LLM 给的 data，
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
}

/* ============================================================================
 * Resolver
 * ========================================================================== */

/** resolver 的产出 */
export interface ResolverOutput {
  /** 新的数据（替换 UiSpec.data） */
  data: Record<string, unknown>
  /**
   * 可回传新的控件定义 —— 例如把标的列表填进 select 候选项。
   * 渲染层会用它覆盖该类型的静态 controls。
   */
  controls?: UiControlDef[]
}

/**
 * 数据层 resolver。
 *
 * resolver 是**代码**，不是 LLM 编的 JSON —— 与「动作参数由工具生成」同一原则。
 * LLM 只影响 `params`，实际取数逻辑固定在这里。
 */
export type UiResolver = (args: Record<string, unknown>) => Promise<ResolverOutput>
