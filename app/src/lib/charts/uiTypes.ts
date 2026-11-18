/**
 * UI 类型的**契约**定义（不含渲染组件）
 *
 * 放在 lib 层是为了让工具层（visualize）能拿到 schema / 控件 / resolver，
 * 而不必把 .vue 组件拉进 LLM 调用链。
 * 渲染组件由 components/agent/ui/registerUiTypes.ts 在注册时补上。
 */

import {
  HISTOGRAM_DESCRIPTION,
  MATRIX_DESCRIPTION,
  METRIC_DESCRIPTION,
  MONTE_CARLO_DESCRIPTION,
  TABLE_DESCRIPTION,
  TIME_SERIES_DESCRIPTION,
  histogramSchema,
  matrixSchema,
  metricSchema,
  monteCarloSchema,
  tableSchema,
  timeSeriesSchema,
} from './schemas'
import { BENCHMARK_OPTIONS, DEFAULT_BENCHMARK } from './resolvers'
import type { UiControlDef, UiTypeDefinition } from './types'

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

/**
 * 全部 UI 类型的契约。
 *
 * 通用型：LLM 自己给 data，只有呈现层控件。
 * 领域型：数据由 resolver 出，带数据层控件（换基准、换标的）。
 */
export const UI_TYPE_DEFS: UiTypeDefinition[] = [
  /* ---------- 通用型 ---------- */
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

  /* ---------- 领域型（resolver 驱动） ---------- */
  {
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
    resolver: 'backtest_performance',
    defaults: {
      benchmark: DEFAULT_BENCHMARK,
      chart_type: 'line',
      smooth: false,
      legend: true,
    },
    compact: true,
    exportable: true,
  },
  {
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
    ],
    resolver: 'symbol_price',
    defaults: { chart_type: 'line', smooth: true, legend: true },
    compact: true,
    exportable: true,
  },
  {
    id: 'mc_paths',
    label: '蒙特卡洛路径',
    icon: 'dice',
    schema: monteCarloSchema,
    schemaDescription: MONTE_CARLO_DESCRIPTION,
    controls: [
      {
        key: 'mode',
        label: '路径',
        kind: 'select',
        layer: 'view',
        options: [
          { value: 'worst', label: '最差路径（失效场景）' },
          { value: 'best', label: '最好路径（有效场景）' },
          { value: 'benchmark', label: '基准分位（P10/P50/P90）' },
        ],
        default: 'worst',
      },
    ],
    resolver: 'mc_paths',
    defaults: { mode: 'worst' },
    compact: true,
    exportable: true,
  },
]
