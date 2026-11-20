/**
 * 各 UI 类型的输入约束（zod schema）
 *
 * 这些 schema 就是**工具约束**：visualize 工具用它们校验 LLM 给的 data，
 * 渲染层用同一份自检。校验失败时工具会回一条说明期望形状的错误，让 LLM 自纠。
 */

import { z } from 'zod'

/* ============================================================================
 * 公共片段
 * ========================================================================== */

/** 一列：名字 + 值数组 */
export const uiColumnSchema = z.object({
  n: z.string().describe('列名（序列名）'),
  v: z.array(z.union([z.number(), z.string(), z.null()])).describe('值数组，允许 null 表示缺数据'),
  u: z.string().optional().describe('单位后缀，如 % / ¥'),
  tone: z.enum(['good', 'bad', 'neutral']).optional().describe('语义色；不给则按值符号推断'),
})

/** 图上标记 */
export const uiMarkerSchema = z.object({
  x: z.number().describe('在 x 数组中的下标'),
  series: z.string().optional().describe('所属序列名'),
  kind: z.enum(['buy', 'sell', 'point']).optional().describe('标记类型'),
  label: z.string().optional().describe('标记说明'),
})

/** 时刻条目 —— 图上标记的信息化描述，渲染在图下方的时刻条 */
export const uiMomentSchema = z.object({
  id: z.string().describe('时刻 id'),
  timestamp: z.number().describe('Unix 时间戳（秒）'),
  title: z.string().describe('时刻标题'),
  detail: z.string().optional().describe('详细说明'),
  severity: z.enum(['info', 'positive', 'warning', 'critical']).optional(),
})

/* ============================================================================
 * timeseries
 * ========================================================================== */

export const timeSeriesSchema = z.object({
  x: z.array(z.number()).describe('X 轴：时间戳（秒）或序号，长度必须与每列的 v 相同'),
  cols: z.array(uiColumnSchema).min(1).describe('数据列，每列 { n: 名称, v: 值数组 }'),
  markers: z.array(uiMarkerSchema).optional().describe('图上标记（买卖点等）'),
  moments: z.array(uiMomentSchema).optional().describe('关键时刻列表，渲染在图下方的时刻条'),
  x_label: z.string().optional().describe('X 轴标题'),
  y_label: z.string().optional().describe('Y 轴标题'),
  y_format: z.enum(['number', 'percent', 'currency']).optional().describe('Y 轴数值格式'),
})

export const TIME_SERIES_DESCRIPTION =
  '{ x: number[], cols: [{ n: 列名, v: number[], u?: 单位, tone?: good|bad|neutral }], ' +
  'x_label?, y_label?, y_format?: number|percent|currency, markers?: [{ x: 下标, kind?: buy|sell|point, label? }], ' +
  'moments?: [{ id, timestamp, title, detail?, severity? }] }'

/* ============================================================================
 * histogram
 * ========================================================================== */

export const histogramSchema = z.object({
  bins: z.array(z.number()).describe('分箱边界，长度 = counts.length + 1'),
  counts: z.array(z.number()).describe('每个箱的计数'),
  x_label: z.string().optional(),
  y_label: z.string().optional(),
})

export const HISTOGRAM_DESCRIPTION = '{ bins: number[]（箱边界）, counts: number[]（每箱计数） }'

/* ============================================================================
 * matrix
 * ========================================================================== */

export const matrixSchema = z.object({
  rows: z.array(z.string()).describe('行标签'),
  cols: z.array(z.string()).describe('列标签'),
  values: z.array(z.array(z.number())).describe('二维数值，行列与标签一一对应'),
  x_label: z.string().optional(),
  y_label: z.string().optional(),
  diverging: z.boolean().optional().describe('是否用红-绿双向色阶（相关性矩阵用 true）'),
})

export const MATRIX_DESCRIPTION =
  '{ rows: string[], cols: string[], values: number[][]（二维数值）, diverging?: boolean }'

/* ============================================================================
 * table
 * ========================================================================== */

export const tableSchema = z.object({
  columns: z
    .array(
      z.object({
        k: z.string().describe('列 key，对应 row 里的字段名'),
        label: z.string().describe('显示名'),
        fmt: z.enum(['number', 'percent', 'currency', 'ratio', 'raw']).optional(),
        precision: z.number().optional().describe('小数位'),
        align: z.enum(['left', 'right', 'center']).optional(),
      }),
    )
    .min(1),
  rows: z.array(z.record(z.string(), z.any())).describe('数据行，每行是 { 列key: 值 }'),
  total: z.number().optional().describe('真实总行数（rows 被截断时）'),
})

export const TABLE_DESCRIPTION =
  '{ columns: [{ k: 列key, label: 显示名, fmt?: number|percent|currency|ratio|raw, precision?, align? }], ' +
  'rows: [{ 列key: 值 }], total?: number }'

/* ============================================================================
 * metric
 * ========================================================================== */

export const metricSchema = z.object({
  items: z
    .array(
      z.object({
        key: z.string().describe('指标 key'),
        label: z.string().describe('显示名'),
        value: z.union([z.number(), z.string()]).describe('数值'),
        format: z.enum(['number', 'percent', 'currency', 'ratio', 'raw']).optional(),
        precision: z.number().optional(),
        delta: z.number().optional().describe('与基期的变化'),
        deltaLabel: z.string().optional(),
        tone: z.enum(['good', 'bad', 'neutral']).optional(),
      }),
    )
    .min(1),
})

export const METRIC_DESCRIPTION =
  '{ items: [{ key, label, value: number|string, format?: number|percent|currency|ratio|raw, ' +
  'precision?, delta?, deltaLabel?, tone?: good|bad|neutral }] }'

/* ============================================================================
 * monte_carlo
 * ========================================================================== */

/** 单条蒙特卡洛路径 */
export const mcPathSchema = z.object({
  equity_curve: z.array(z.number()).describe('净值曲线，1.0 为起点'),
  total_return: z.number().optional(),
  max_drawdown: z.number().optional(),
  win_rate: z.number().optional(),
  longest_win_streak: z.number().optional(),
  longest_loss_streak: z.number().optional(),
})

export const monteCarloSchema = z.object({
  worst: z.array(mcPathSchema).describe('最差路径族'),
  best: z.array(mcPathSchema).describe('最好路径族'),
  median: mcPathSchema.optional().describe('P50 中位数路径'),
  p10: mcPathSchema.optional().describe('P10 次差路径'),
  p90: mcPathSchema.optional().describe('P90 次优路径'),
  bar_count: z.number().optional().describe('bar 数量（x 轴范围）'),
})

export const MONTE_CARLO_DESCRIPTION =
  '{ worst: 路径数组, best: 路径数组, median?, p10?, p90?, bar_count? }；' +
  '每条路径 = { equity_curve: number[], total_return?, max_drawdown?, win_rate? }'

/* ============================================================================
 * progress（长任务进度）
 *
 * 只能由工具/动作产生（uiType 里标了 hidden），LLM 不能直接构建 ——
 * 否则模型可以伪造一个订阅，去偷看别人的 job 事件。
 * ========================================================================== */

export const progressSchema = z.object({
  status: z.enum(['running', 'done', 'failed']).describe('任务状态'),
  label: z.string().optional().describe('当前阶段或一句话说明'),
  steps: z.array(z.string()).optional().describe('阶段名列表；有就渲染成步骤条'),
  step: z.number().optional().describe('当前阶段下标（对应 steps）'),
  detail: z.string().optional().describe('补充说明，如导入行数、失败原因'),
})

export const PROGRESS_DESCRIPTION =
  '{ status: running|done|failed, label?, steps?: string[], step?: number, detail? }'
