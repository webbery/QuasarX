/**
 * UI 类型的**契约**定义（不含渲染组件）
 *
 * 放在 lib 层是为了让工具层（ui）能拿到 schema / 控件 / resolver，
 * 而不必把 .vue 组件拉进 LLM 调用链。
 * 渲染组件由 components/agent/ui/registerUiTypes.ts 在注册时补上。
 */

import {
  HISTOGRAM_DESCRIPTION,
  MATRIX_DESCRIPTION,
  METRIC_DESCRIPTION,
  MONTE_CARLO_DESCRIPTION,
  PROGRESS_DESCRIPTION,
  TABLE_DESCRIPTION,
  TIME_SERIES_DESCRIPTION,
  histogramSchema,
  matrixSchema,
  metricSchema,
  monteCarloSchema,
  progressSchema,
  tableSchema,
  timeSeriesSchema,
} from './schemas'
import { BENCHMARK_OPTIONS, DEFAULT_BENCHMARK } from './resolvers'
import type { UiControlDef, UiReducer, UiTypeDefinition } from './types'

/** 图形类型（呈现层控件） */
const CHART_TYPE_OPTIONS = [
  { value: 'line', label: '折线' },
  { value: 'area', label: '面积' },
  { value: 'bar', label: '柱状' },
  { value: 'scatter', label: '散点' },
]

/** 通用时序图的呈现层控件 */
const TIMESERIES_VIEW_CONTROLS: UiControlDef[] = [
  {
    key: 'chart_type',
    label: '图形',
    kind: 'select',
    layer: 'view',
    options: CHART_TYPE_OPTIONS,
    default: 'line',
  },
  { key: 'smooth', label: '平滑', kind: 'toggle', layer: 'view', default: false },
  { key: 'legend', label: '图例', kind: 'toggle', layer: 'view', default: false },
]

/* ============================================================================
 * progress 的事件合并语义
 * ========================================================================== */

/** 行情下载链路的阶段（顺序与 service 的 DataStage 一致） */
const DOWNLOAD_STAGE_LABEL = ['下载数据', '数据体检', '入库', '补数据重试'] as const

/**
 * progress 的 reducer：把一条 SSE 事件并进进度数据。
 *
 * 同时吃两种载荷：
 * - `quote_download`：{ status, stage, freq, total, total_rows }
 *   stage 是 service 的 DataStage 序号，按下标取标签 —— 用序号而不是名字，
 *   两边少一处要对齐的字符串。
 * - 任务状态查询（推送漏了时的兜底）：{ status, total_rows, error, progress }
 *   形状与上面同源，status 取 running/done/error。
 * - `backtest_progress`：{ strategy, run_id, progress, message }
 *
 * 回测的 progress 值是阶段标记（0 → 0.6 → 0.7 → 0.8，永远到不了 1.0），
 * 所以这里**不换算百分比**，只用 message 推进文案，终态也从 message 里认。
 * 等后端给 backtest_progress 补上 status 字段后，可以删掉最后那个分支。
 */
const progressReducer: UiReducer = (data, event) => {
  const next = { ...data }
  const status = typeof event.status === 'string' ? event.status : ''

  // 阶段推进（行情下载）
  const stage = Number(event.stage)
  if (Number.isInteger(stage) && stage >= 0 && stage < DOWNLOAD_STAGE_LABEL.length) {
    next.step = stage
    next.label = DOWNLOAD_STAGE_LABEL[stage]
  }

  // 阶段内的逐标的进度（行情下载）
  const done = Number(event.done)
  const total = Number(event.total)
  if (Number.isInteger(done) && Number.isInteger(total) && total > 0 && done > 0) {
    next.label = `${next.label ?? '下载数据'} ${done}/${total}`
  }

  // 文案推进（回测的 message 自带阶段信息）
  const message = typeof event.message === 'string' ? event.message : ''
  if (message) next.label = message

  if (status === 'started' || status === 'progress' || status === 'running') {
    next.status = 'running'
  } else if (status === 'done') {
    next.status = 'done'
    next.step = DOWNLOAD_STAGE_LABEL.length
    next.label = '完成'
    if (event.total_rows !== undefined) next.detail = `导入 ${event.total_rows} 行`
  } else if (status === 'failed' || status === 'error') {
    next.status = 'failed'
    const err = event.error ?? event.message
    if (err) next.detail = String(err)
  } else if (message) {
    // 没有 status 字段的链路（backtest_progress）：从文案里认终态
    if (/出错|失败|error/i.test(message)) next.status = 'failed'
    else if (/完成|finished/i.test(message)) next.status = 'done'
    else next.status = 'running'
  }

  return next
}

/**
 * 全部 UI 类型的契约。
 *
 * 通用型：LLM 自己给 data，只有呈现层控件。
 * 领域型：数据由 resolver 出，带数据层控件（换基准、换标的）。
 */
export const UI_TYPE_DEFS: UiTypeDefinition[] = [  /* ---------- 通用型 ---------- */
  {
    id: 'timeseries',
    label: '时序图',
    icon: 'chart-line',
    schema: timeSeriesSchema,
    schemaDescription: TIME_SERIES_DESCRIPTION,
    controls: TIMESERIES_VIEW_CONTROLS,
    defaults: { chart_type: 'line', smooth: false, legend: false },
    compact: true,
    exportable: true,
  },
  {
    id: 'histogram',
    label: '分布直方图',
    icon: 'chart-bar',
    schema: histogramSchema,
    schemaDescription: HISTOGRAM_DESCRIPTION,
    controls: [],
    compact: true,
    exportable: true,
  },
  {
    id: 'matrix',
    label: '热力图',
    icon: 'table-cells',
    schema: matrixSchema,
    schemaDescription: MATRIX_DESCRIPTION,
    controls: [
      { key: 'diverging', label: '双向色阶', kind: 'toggle', layer: 'view', default: false },
    ],
    compact: true,
    exportable: true,
  },
  {
    id: 'table',
    label: '表格',
    icon: 'table',
    schema: tableSchema,
    schemaDescription: TABLE_DESCRIPTION,
    controls: [
      {
        key: 'max_rows',
        label: '显示行数',
        kind: 'slider',
        layer: 'view',
        min: 5,
        max: 200,
        step: 5,
        suffix: ' 行',
        default: 50,
      },
    ],
    compact: true,
    exportable: true,
  },
  {
    id: 'metric',
    label: '指标',
    icon: 'gauge',
    schema: metricSchema,
    schemaDescription: METRIC_DESCRIPTION,
    controls: [],
    compact: true,
  },

  /* ---------- 内部型（只能由工具/动作产生，不对 LLM 暴露） ---------- */
  {
    id: 'progress',
    label: '任务进度',
    icon: 'spinner',
    schema: progressSchema,
    schemaDescription: PROGRESS_DESCRIPTION,
    controls: [],
    intake: 'stream',
    compact: true,
    // 不让 LLM 直接构建：否则它可以伪造订阅去偷看别人的任务事件
    hidden: true,
    reducer: progressReducer,
  },
]
