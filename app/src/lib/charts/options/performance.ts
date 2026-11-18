/**
 * 策略绩效（累计收益 + 基准 + 回撤）→ ECharts option
 *
 * 纯函数。report 的 PerformanceChart 与 Agent 的 performance 视图共用。
 *
 * 布局要点：回撤单独占下方一格（双 grid + 双 Y 轴），
 * 有回撤时上方是累计收益/基准，下方是回撤；没有回撤时退化成单 grid。
 */

import * as echarts from 'echarts'
import { createBaseChartOption } from '@/components/report/composables/useECharts'

export interface PerformanceInput {
  /** X 轴日期标签 */
  dates: string[]
  /** 策略累计收益（百分比数值） */
  cumulativeReturns: Array<number | null>
  /** 基准累计收益（百分比数值）；给了才画基准线 */
  benchmarkCumulative?: Array<number | null>
  /** 基准名 */
  benchmarkName?: string
  /** 回撤序列（百分比数值，负值） */
  drawdown?: Array<number | null>
  /** 持仓区间，用于高亮 markArea */
  holdingPeriods?: Array<[string, string]>
}

/** 日期标签格式化：已经是 YYYY-MM-DD 就原样返回 */
function axisLabelFormatter(value: string): string {
  if (!value) return value
  if (/^\d{4}-\d{2}-\d{2}$/.test(value)) return value
  const date = new Date(value)
  if (isNaN(date.getTime())) return value
  const Y = date.getFullYear()
  const M = String(date.getMonth() + 1).padStart(2, '0')
  const D = String(date.getDate()).padStart(2, '0')
  return `${Y}-${M}-${D}`
}

/**
 * 构建绩效图 option。
 */
export function buildPerformanceOption(input: PerformanceInput): any {
  const {
    dates,
    cumulativeReturns,
    benchmarkCumulative,
    benchmarkName,
    drawdown,
    holdingPeriods,
  } = input

  const hasBenchmark = !!benchmarkCumulative && benchmarkCumulative.length > 0
  const dd = drawdown ?? []
  const hasDrawdown = dd.length > 0 && dd.some((v) => v !== null && v < 0)

  const markAreaData = (holdingPeriods ?? []).map(([start, end]) => [
    { xAxis: start },
    { xAxis: end },
  ])

  const series: any[] = [
    {
      name: '累计收益',
      type: 'line',
      xAxisIndex: 0,
      yAxisIndex: 0,
      data: cumulativeReturns,
      smooth: true,
      lineStyle: { width: 3, color: '#2962ff' },
      areaStyle: {
        color: new echarts.graphic.LinearGradient(0, 0, 0, 1, [
          { offset: 0, color: 'rgba(41, 98, 255, 0.3)' },
          { offset: 1, color: 'rgba(41, 98, 255, 0.05)' },
        ]),
      },
      markArea:
        markAreaData.length > 0
          ? {
              silent: true,
              data: markAreaData,
              itemStyle: { color: 'rgba(0, 200, 83, 0.08)' },
            }
          : undefined,
    },
  ]

  if (hasBenchmark) {
    series.push({
      name: benchmarkName || '基准',
      type: 'line',
      xAxisIndex: 0,
      yAxisIndex: 0,
      data: benchmarkCumulative,
      smooth: true,
      lineStyle: { width: 2, color: '#ff9800', type: 'dashed' },
    })
  }

  if (hasDrawdown) {
    series.push({
      name: '回撤',
      type: 'line',
      xAxisIndex: 1,
      yAxisIndex: 1,
      data: dd,
      smooth: true,
      lineStyle: { width: 1.5, color: '#ef5350' },
      areaStyle: {
        color: new echarts.graphic.LinearGradient(0, 0, 0, 1, [
          { offset: 0, color: 'rgba(239, 83, 80, 0.25)' },
          { offset: 1, color: 'rgba(239, 83, 80, 0.02)' },
        ]),
      },
      markArea:
        markAreaData.length > 0
          ? {
              silent: true,
              data: markAreaData,
              itemStyle: { color: 'rgba(0, 200, 83, 0.06)' },
            }
          : undefined,
    })
  }

  return createBaseChartOption({
    tooltip: {
      trigger: 'axis',
      axisPointer: { type: 'cross' },
      backgroundColor: 'rgba(26, 34, 54, 0.9)',
      borderColor: '#2a3449',
      textStyle: { color: '#e0e0e0' },
      formatter: function (params: any) {
        let result = `<div style="margin: 0 0 5px 0; font-weight: bold;">${params[0].axisValue}</div>`
        params.forEach((item: any) => {
          const value = typeof item.value === 'number' ? item.value.toFixed(2) : item.value
          let color = '#2962ff'
          if (item.seriesName === '回撤') color = '#ef5350'
          else if (item.seriesName !== '累计收益') color = '#ff9800'
          result += `<div>${item.marker} ${item.seriesName}: <span style="color: ${color}; font-weight: bold;">${value}%</span></div>`
        })
        return result
      },
    },
    legend: {
      data: hasBenchmark
        ? ['累计收益', benchmarkName || '基准', '回撤']
        : ['累计收益', '回撤'],
      textStyle: { color: '#e0e0e0' },
      top: 10,
    },
    grid: hasDrawdown
      ? [
          { left: '3%', right: '4%', top: '12%', height: '52%' },
          { left: '3%', right: '4%', top: '72%', height: '20%' },
        ]
      : { left: '3%', right: '4%', bottom: '3%', containLabel: true },
    xAxis: hasDrawdown
      ? [
          {
            type: 'category',
            data: dates,
            gridIndex: 0,
            axisLine: { lineStyle: { color: '#6E7079' } },
            axisLabel: { show: false },
            splitLine: { show: false },
          },
          {
            type: 'category',
            data: dates,
            gridIndex: 1,
            axisLine: { lineStyle: { color: '#6E7079' } },
            axisLabel: { color: '#a0aec0', formatter: axisLabelFormatter },
            splitLine: { show: false },
          },
        ]
      : {
          type: 'category',
          data: dates,
          axisLine: { lineStyle: { color: '#6E7079' } },
          axisLabel: { color: '#a0aec0', formatter: axisLabelFormatter },
        },
    yAxis: hasDrawdown
      ? [
          {
            type: 'value',
            gridIndex: 0,
            axisLabel: { formatter: '{value}%', color: '#a0aec0' },
            axisLine: { lineStyle: { color: '#6E7079' } },
            splitLine: { lineStyle: { color: '#2a3449', type: 'dashed' } },
          },
          {
            type: 'value',
            gridIndex: 1,
            axisLabel: { formatter: '{value}%', color: '#a0aec0' },
            axisLine: { lineStyle: { color: '#6E7079' } },
            splitLine: { lineStyle: { color: '#2a3449', type: 'dashed' } },
          },
        ]
      : {
          type: 'value',
          axisLabel: { formatter: '{value}%', color: '#a0aec0' },
          axisLine: { lineStyle: { color: '#6E7079' } },
          splitLine: { lineStyle: { color: '#2a3449', type: 'dashed' } },
        },
    dataZoom: hasDrawdown
      ? [
          {
            type: 'inside',
            xAxisIndex: [0, 1],
            filterMode: 'filter',
            zoomOnMouseWheel: true,
            moveOnMouseMove: true,
            moveOnMouseWheel: false,
          },
          {
            type: 'slider',
            xAxisIndex: [0, 1],
            bottom: '2%',
            height: 18,
            borderColor: '#2a3449',
            fillerColor: 'rgba(41, 98, 255, 0.2)',
            handleStyle: { color: '#2962ff' },
            textStyle: { color: '#a0aec0' },
          },
        ]
      : [
          {
            type: 'inside',
            xAxisIndex: 0,
            filterMode: 'filter',
            zoomOnMouseWheel: true,
            moveOnMouseMove: true,
            moveOnMouseWheel: false,
          },
          {
            type: 'slider',
            xAxisIndex: 0,
            bottom: '3%',
            height: 20,
            borderColor: '#2a3449',
            fillerColor: 'rgba(41, 98, 255, 0.2)',
            handleStyle: { color: '#2962ff' },
            textStyle: { color: '#a0aec0' },
          },
        ],
    series,
  })
}
