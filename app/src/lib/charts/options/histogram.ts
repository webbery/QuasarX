/**
 * 直方图 / 热力图 → ECharts option
 *
 * 同样是纯函数，供 Agent 视图与（未来的）report 壳共用。
 */

import { createBaseChartOption } from '@/components/report/composables/useECharts'

const POSITIVE = '#00c853'
const NEGATIVE = '#ef5350'

export interface HistogramData {
  bins: number[]
  counts: number[]
  x_label?: string
  y_label?: string
}

/**
 * 构建直方图 option。
 * bins 通常是边界数组（长度 = counts.length + 1）。
 */
export function buildHistogramOption(data: HistogramData): any {
  const bins = data.bins ?? []
  const counts = data.counts ?? []

  const labels = counts.map((_, i) => {
    const left = bins[i]
    const right = bins[i + 1] ?? bins[i]
    if (left === undefined) return String(i)
    return `${(left * 100).toFixed(1)}~${(right * 100).toFixed(1)}%`
  })

  const colors = counts.map((_, i) => {
    const mid = (bins[i] + (bins[i + 1] ?? bins[i])) / 2
    return mid < 0 ? NEGATIVE : POSITIVE
  })

  return createBaseChartOption({
    animation: false,
    tooltip: {
      trigger: 'axis',
      backgroundColor: 'rgba(26, 34, 54, 0.94)',
      borderColor: '#2a3449',
      textStyle: { color: '#e0e0e0', fontSize: 11 },
    },
    grid: { left: 48, right: 20, top: 18, bottom: 46, containLabel: false },
    xAxis: {
      type: 'category',
      data: labels,
      axisLabel: { color: '#6f7b8a', fontSize: 10, rotate: 30, hideOverlap: true },
      axisLine: { lineStyle: { color: '#2a3449' } },
      axisTick: { show: false },
    },
    yAxis: {
      type: 'value',
      name: data.y_label,
      nameTextStyle: { color: '#6f7b8a', fontSize: 11 },
      axisLabel: { color: '#a0aec0', fontSize: 11 },
      splitLine: { lineStyle: { color: '#2a3449', type: 'dashed' } },
    },
    series: [
      {
        type: 'bar',
        data: counts.map((v, i) => ({ value: v, itemStyle: { color: colors[i], opacity: 0.75 } })),
        barMaxWidth: 26,
      },
    ],
  })
}

export interface MatrixData {
  rows: string[]
  cols: string[]
  values: number[][]
  x_label?: string
  y_label?: string
  /** 是否用红-绿双向色阶（相关性矩阵用 true） */
  diverging?: boolean
}

/** 构建热力图 option */
export function buildMatrixOption(data: MatrixData): any {
  const points: number[][] = []
  let min = Infinity
  let max = -Infinity

  data.values.forEach((row, r) => {
    row.forEach((v, c) => {
      points.push([c, r, v])
      if (Number.isFinite(v)) {
        min = Math.min(min, v)
        max = Math.max(max, v)
      }
    })
  })

  if (!Number.isFinite(min)) {
    min = 0
    max = 1
  }

  return createBaseChartOption({
    animation: false,
    tooltip: {
      backgroundColor: 'rgba(26, 34, 54, 0.94)',
      borderColor: '#2a3449',
      textStyle: { color: '#e0e0e0', fontSize: 11 },
    },
    grid: { left: 64, right: 60, top: 16, bottom: 48, containLabel: false },
    xAxis: {
      type: 'category',
      data: data.cols,
      splitArea: { show: true },
      axisLabel: { color: '#6f7b8a', fontSize: 10 },
      axisLine: { lineStyle: { color: '#2a3449' } },
    },
    yAxis: {
      type: 'category',
      data: data.rows,
      splitArea: { show: true },
      axisLabel: { color: '#6f7b8a', fontSize: 10 },
      axisLine: { lineStyle: { color: '#2a3449' } },
    },
    visualMap: {
      min,
      max,
      calculable: false,
      orient: 'vertical',
      right: 4,
      top: 'middle',
      itemWidth: 10,
      itemHeight: 90,
      textStyle: { color: '#6f7b8a', fontSize: 10 },
      inRange: {
        color: data.diverging
          ? ['#ef5350', '#1a2236', '#00c853']
          : ['#1a2236', '#2962ff'],
      },
    },
    series: [
      {
        type: 'heatmap',
        data: points,
        label: { show: false },
        emphasis: { itemStyle: { borderColor: '#e0e0e0', borderWidth: 1 } },
      },
    ],
  })
}
