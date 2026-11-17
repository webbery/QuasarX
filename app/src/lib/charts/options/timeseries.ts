/**
 * 列式时序 → ECharts option
 *
 * 纯函数：输入列式数据 + 呈现参数，输出 option。不碰 DOM、不管生命周期，
 * 所以 report 壳与 Agent 视图可以共用。
 */

import { createBaseChartOption } from '@/components/report/composables/useECharts'

/** 主题调色板（与 report 模块一致） */
const PALETTE = ['#2962ff', '#ff9800', '#00c853', '#ff6d00', '#a0aec0', '#9a67d1']

export interface TimeSeriesColumn {
  n: string
  v: Array<number | string | null>
  u?: string
  tone?: 'good' | 'bad' | 'neutral'
}

export interface TimeSeriesMarker {
  x: number
  series?: string
  kind?: 'buy' | 'sell' | 'point'
  label?: string
}

export interface TimeSeriesData {
  x: number[]
  cols: TimeSeriesColumn[]
  markers?: TimeSeriesMarker[]
  x_label?: string
  y_label?: string
  y_format?: 'number' | 'percent' | 'currency'
}

export interface TimeSeriesOptions {
  /** 图形类型 */
  chartType?: 'line' | 'area' | 'bar' | 'scatter'
  /** 是否平滑 */
  smooth?: boolean
  /** 第二条 Y 轴的列下标 */
  secondaryAxisIndex?: number
  /** 第二条 Y 轴的标题 */
  secondaryAxisLabel?: string
  /** 显示图例 */
  legend?: boolean
  /** 只画这些列的下标（为空表示全画） */
  visibleColumns?: number[]
}

/** 百分比类数据 ×100 */
function scale(values: Array<number | string | null>, format?: string): Array<number | null> {
  if (format !== 'percent') {
    return values.map((v) => (typeof v === 'number' && Number.isFinite(v) ? v : null))
  }
  return values.map((v) => (typeof v === 'number' && Number.isFinite(v) ? v * 100 : null))
}

/** 时间戳 → 'MM-DD'；非时间戳（序号）原样返回 */
function formatX(ts: number, asDate: boolean): string {
  if (!asDate) return String(ts)
  const d = new Date(ts > 1e11 ? ts : ts * 1000)
  const mm = String(d.getMonth() + 1).padStart(2, '0')
  const dd = String(d.getDate()).padStart(2, '0')
  return `${mm}-${dd}`
}

/** 判断 x 是否像时间戳（10 位秒级或 13 位毫秒级） */
function looksLikeTimestamp(x: number[]): boolean {
  if (!x.length) return false
  const first = x[0]
  return first > 1e9 && first < 4e12
}

/** 语义色 → 实际颜色 */
function toneColor(tone?: string): string | undefined {
  if (tone === 'good') return '#00c853'
  if (tone === 'bad') return '#ef5350'
  return undefined
}

/**
 * 构建时序图的 ECharts option。
 */
export function buildTimeseriesOption(
  data: TimeSeriesData,
  opts: TimeSeriesOptions = {},
): any {
  const chartType = opts.chartType ?? 'line'
  const asDate = looksLikeTimestamp(data.x ?? [])
  const categories = (data.x ?? []).map((ts) => formatX(ts, asDate))
  const visible = opts.visibleColumns?.length
    ? data.cols.filter((_, i) => opts.visibleColumns!.includes(i))
    : data.cols

  const hasSecondary =
    opts.secondaryAxisIndex !== undefined &&
    opts.secondaryAxisIndex >= 0 &&
    opts.secondaryAxisIndex < data.cols.length

  const series: any[] = visible.map((col) => {
    const originalIndex = data.cols.indexOf(col)
    const color = toneColor(col.tone) ?? PALETTE[originalIndex % PALETTE.length]
    const isSecondary = hasSecondary && originalIndex === opts.secondaryAxisIndex

    const base: any = {
      name: col.n,
      type: chartType === 'area' ? 'line' : chartType,
      data: scale(col.v, data.y_format),
      showSymbol: chartType === 'scatter',
      yAxisIndex: isSecondary ? 1 : 0,
      lineStyle: { color, width: 2 },
      itemStyle: { color },
      emphasis: { focus: 'series' },
      smooth: opts.smooth ?? false,
    }

    if (chartType === 'bar') {
      base.barMaxWidth = 24
      base.itemStyle = { color, borderRadius: [2, 2, 0, 0] }
    }
    if (chartType === 'area') {
      base.areaStyle = { color, opacity: 0.18 }
    }
    if (col.u && col.u !== '%') {
      base.name = `${col.n}(${col.u})`
    }

    return base
  })

  // 标记：打到对应列上
  const markers = data.markers ?? []
  if (markers.length && visible.length) {
    const buyCol = visible.findIndex((c) => c.n.includes('买'))
    const sellCol = visible.findIndex((c) => c.n.includes('卖'))
    const target = visible[0]
    const markPoints: any[] = []

    for (const m of markers) {
      const colIndex =
        m.kind === 'buy' && buyCol >= 0 ? buyCol : m.kind === 'sell' && sellCol >= 0 ? sellCol : 0
      const col = visible[colIndex]
      if (!col) continue
      const value = scale(col.v, data.y_format)[m.x]
      if (value === null || value === undefined) continue
      markPoints.push({
        coord: [m.x, value],
        name: m.label ?? m.kind ?? '',
        symbol: m.kind === 'sell' ? 'triangle' : 'triangle',
        symbolRotate: m.kind === 'sell' ? 180 : 0,
        symbolSize: 9,
        itemStyle: {
          color: m.kind === 'sell' ? '#ff6d00' : '#00c853',
        },
        label: { show: false },
      })
    }
    if (markPoints.length) {
      series[0] = { ...series[0], markPoint: { silent: true, data: markPoints } }
    }
    void target
  }

  const yAxis: any[] = [
    {
      type: 'value',
      name: data.y_label,
      nameTextStyle: { color: '#6f7b8a', fontSize: 11 },
      axisLabel: {
        color: '#a0aec0',
        fontSize: 11,
        formatter: (v: number) =>
          data.y_format === 'percent' ? `${v.toFixed(0)}%` : `${v}`,
      },
      splitLine: { lineStyle: { color: '#2a3449', type: 'dashed' } },
      scale: data.y_format !== 'percent',
    },
  ]

  if (hasSecondary) {
    yAxis.push({
      type: 'value',
      name: opts.secondaryAxisLabel,
      nameTextStyle: { color: '#6f7b8a', fontSize: 11 },
      axisLabel: { color: '#a0aec0', fontSize: 11 },
      splitLine: { show: false },
    })
  }

  return createBaseChartOption({
    animation: false,
    tooltip: {
      trigger: 'axis',
      axisPointer: { type: 'cross' },
      backgroundColor: 'rgba(26, 34, 54, 0.94)',
      borderColor: '#2a3449',
      textStyle: { color: '#e0e0e0', fontSize: 11 },
      valueFormatter: (v: any) =>
        typeof v === 'number'
          ? data.y_format === 'percent'
            ? `${v.toFixed(2)}%`
            : v.toFixed(2)
          : v,
    },
    legend: {
      show: opts.legend ?? false,
      textStyle: { color: '#e0e0e0', fontSize: 11 },
      top: 0,
    },
    grid: {
      left: 52,
      right: hasSecondary ? 56 : 20,
      top: opts.legend ? 30 : 18,
      bottom: 34,
      containLabel: false,
    },
    xAxis: {
      type: 'category',
      data: categories,
      boundaryGap: chartType === 'bar',
      axisLabel: {
        color: '#6f7b8a',
        fontSize: 10,
        hideOverlap: true,
      },
      axisLine: { lineStyle: { color: '#2a3449' } },
      axisTick: { show: false },
    },
    yAxis,
    series,
  })
}
