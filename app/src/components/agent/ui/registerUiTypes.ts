/**
 * UI 类型注册
 *
 * 每条声明 = 「输入约束（工具约束）+ 参数控件 + 渲染组件 + 数据层 resolver」。
 *
 * 两类：
 *   - 通用型（timeseries / histogram / matrix / table / metric）：
 *     LLM 自己给 data，只有呈现层控件（图形、平滑、显示行数……）。
 *   - 领域型（performance / price）：
 *     数据由 resolver 出，带数据层控件（换基准、换标的），用户一改就回源。
 */

import { registerUiType } from '@/lib/charts/registry'
import {
  HISTOGRAM_DESCRIPTION,
  MATRIX_DESCRIPTION,
  METRIC_DESCRIPTION,
  TABLE_DESCRIPTION,
  TIME_SERIES_DESCRIPTION,
  histogramSchema,
  matrixSchema,
  metricSchema,
  tableSchema,
  timeSeriesSchema,
} from '@/lib/charts/schemas'
import { BENCHMARK_OPTIONS, DEFAULT_BENCHMARK } from '@/lib/charts/resolvers'
import type { UiControlDef } from '@/lib/charts/types'
import TimeSeriesView from './TimeSeriesView.vue'
import HistogramView from './HistogramView.vue'
import MatrixView from './MatrixView.vue'
import TableView from './TableView.vue'
import MetricView from './MetricView.vue'

let registered = false

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

export function registerUiTypes(): void {
  if (registered) return
  registered = true

  /* ---------- 通用型：LLM 自己给数据 ---------- */

  registerUiType({
    id: 'timeseries',
    label: '时序图',
    icon: 'chart-line',
    schema: timeSeriesSchema,
    schemaDescription: TIME_SERIES_DESCRIPTION,
    controls: TIMESERIES_VIEW_CONTROLS,
    component: TimeSeriesView,
    defaults: { chart_type: 'line', smooth: false, legend: false },
    compact: true,
    exportable: true,
  })

  registerUiType({
    id: 'histogram',
    label: '分布直方图',
    icon: 'chart-bar',
    schema: histogramSchema,
    schemaDescription: HISTOGRAM_DESCRIPTION,
    controls: [],
    component: HistogramView,
    compact: true,
    exportable: true,
  })

  registerUiType({
    id: 'matrix',
    label: '热力图',
    icon: 'table-cells',
    schema: matrixSchema,
    schemaDescription: MATRIX_DESCRIPTION,
    controls: [
      { key: 'diverging', label: '双向色阶', kind: 'toggle', layer: 'view', default: false },
    ],
    component: MatrixView,
    compact: true,
    exportable: true,
  })

  registerUiType({
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
    component: TableView,
    compact: true,
    exportable: true,
  })

  registerUiType({
    id: 'metric',
    label: '指标',
    icon: 'gauge',
    schema: metricSchema,
    schemaDescription: METRIC_DESCRIPTION,
    controls: [],
    component: MetricView,
    compact: true,
  })

  /* ---------- 领域型：数据由 resolver 出，带数据层控件 ---------- */

  registerUiType({
    id: 'performance',
    label: '策略绩效',
    icon: 'chart-line',
    schema: timeSeriesSchema,
    schemaDescription: TIME_SERIES_DESCRIPTION,
    controls: [
      {
        key: 'benchmark',
        label: '基准',
        kind: 'select',
        layer: 'data',
        options: BENCHMARK_OPTIONS,
        default: DEFAULT_BENCHMARK,
      },
      ...TIMESERIES_VIEW_CONTROLS,
    ],
    component: TimeSeriesView,
    resolver: 'backtest_performance',
    defaults: {
      benchmark: DEFAULT_BENCHMARK,
      chart_type: 'line',
      smooth: false,
      legend: true,
    },
    compact: true,
    exportable: true,
  })

  registerUiType({
    id: 'price',
    label: '价格趋势',
    icon: 'chart-line',
    schema: timeSeriesSchema,
    schemaDescription: TIME_SERIES_DESCRIPTION,
    controls: [
      {
        key: 'symbol',
        label: '标的',
        kind: 'select',
        layer: 'data',
        options: [],
        default: '',
      },
      ...TIMESERIES_VIEW_CONTROLS,
    ],
    component: TimeSeriesView,
    resolver: 'symbol_price',
    defaults: { chart_type: 'line', smooth: true, legend: true },
    compact: true,
    exportable: true,
  })
}
