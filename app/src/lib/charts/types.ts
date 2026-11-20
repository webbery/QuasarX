/**
 * 图表类数据形状
 *
 * 通用协议（UiSpec / UiTypeDefinition / UiBinding / 控件定义）已经搬到
 * `@/lib/ui/protocol` —— 那部分跟「图」无关，产物信封也要用。
 *
 * 这里只剩**具体视图的数据形状**：列式数据、表格列、指标条目、时刻条目。
 * 相关 zod 约束在同目录 schemas.ts，两者一一对应。
 */

// 协议原样从通用层转出：外部（含 lib/charts/index.ts）继续从本文件导入即可，
// 不需要改引用路径。
export * from '@/lib/ui/protocol'

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

/** 进度条状态（progress 类型的 data 形状） */
export interface UiProgressData {
  status: 'running' | 'done' | 'failed'
  /** 当前阶段或一句话说明 */
  label?: string
  /** 阶段名列表（有就画成步骤条） */
  steps?: string[]
  /** 当前阶段下标（对应 steps） */
  step?: number
  /** 补充说明，如导入行数、失败原因 */
  detail?: string
}
