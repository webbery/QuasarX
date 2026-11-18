/**
 * 蒙特卡洛路径 → ECharts option
 *
 * 纯函数。report 的 MonteCarloPathsChart 与 Agent 的 mc_paths 视图共用。
 *
 * 三种模式：
 *   - worst    最差路径族（失效场景）
 *   - best     最好路径族（有效场景）
 *   - benchmark 基准分位（P10 / P50 / P90）
 */

import { createBaseChartOption } from '@/components/report/composables/useECharts'
import type { McPathDetail } from '@/stores/history'

export type MonteCarloMode = 'worst' | 'best' | 'benchmark'

export interface MonteCarloInput {
  mode: MonteCarloMode
  worstPaths: McPathDetail[]
  bestPaths: McPathDetail[]
  medianPath: McPathDetail | null
  p10Path: McPathDetail | null
  p90Path: McPathDetail | null
  /** bar 数量（x 轴标签范围） */
  barCount?: number
}

/** 降采样 equity_curve 到 maxPoints 个点 */
export function downsampleCurve(curve: number[], maxPoints: number): number[] {
  if (curve.length <= maxPoints) return curve
  const ratio = (curve.length - 1) / (maxPoints - 1)
  const result: number[] = []
  for (let i = 0; i < maxPoints; i++) {
    result.push(curve[Math.round(i * ratio)])
  }
  return result
}

/** 净值曲线 → 百分比序列 */
function toPercent(curve: number[]): number[] {
  return curve.map((v) => Number(((v - 1) * 100).toFixed(2)))
}

/**
 * 构建蒙特卡洛路径图 option。
 */
export function buildMonteCarloOption(input: MonteCarloInput): any {
  const maxPoints = 500 // 前端降采样到 500 点
  const curveLen = input.barCount || 252
  const xLabels: string[] = []
  for (let i = 0; i < Math.min(curveLen, maxPoints); i++) xLabels.push(String(i))

  const series: any[] = []
  const isBenchmark = input.mode === 'benchmark'

  if (isBenchmark) {
    const benchmarkPaths = [
      { path: input.p10Path, name: 'P10 (次差)', color: '#ff6d00', dashType: 'dotted' },
      { path: input.medianPath, name: 'P50 (中位数)', color: '#a0aec0', dashType: 'dashed' },
      { path: input.p90Path, name: 'P90 (次优)', color: '#00c853', dashType: 'dotted' },
    ]

    for (const bp of benchmarkPaths) {
      if (!bp.path) continue
      const data = toPercent(downsampleCurve(bp.path.equity_curve, maxPoints))
      series.push({
        name: bp.name,
        type: 'line',
        data,
        smooth: true,
        lineStyle: {
          width: bp.name.includes('中位数') ? 3 : 2,
          color: bp.color,
          type: bp.dashType as 'solid' | 'dashed' | 'dotted',
        },
        symbol: 'none',
      })
    }
  } else {
    const paths = input.mode === 'worst' ? input.worstPaths : input.bestPaths
    const color = input.mode === 'worst' ? '#f44336' : '#00c853'

    for (let i = 0; i < paths.length; i++) {
      const path = paths[i]
      const data = toPercent(downsampleCurve(path.equity_curve, maxPoints))
      const alpha = paths.length > 1 ? Math.max(0.15, 0.6 - i * 0.04) : 0.8

      series.push({
        name: `#${i + 1}`,
        type: 'line',
        data,
        smooth: true,
        lineStyle: { width: 1.5, color },
        itemStyle: { opacity: alpha },
        symbol: 'none',
        // 路径详情挂在 series 上，tooltip 里展示
        _pathDetail: path,
      })
    }
  }

  // 50% 爆仓阈值线
  series.push({
    name: '50% 阈值',
    type: 'line',
    data: Array.from({ length: xLabels.length }, () => -50),
    lineStyle: { width: 1, color: '#f44336', type: 'dotted' },
    symbol: 'none',
  })

  return createBaseChartOption({
    tooltip: {
      trigger: 'axis',
      axisPointer: { type: 'cross' },
      backgroundColor: 'rgba(26, 34, 54, 0.95)',
      borderColor: '#2a3449',
      textStyle: { color: '#e0e0e0', fontSize: 12 },
      formatter: function (params: any) {
        if (!params || params.length === 0) return ''

        let result = `<div style="margin: 0 0 6px 0; font-weight: bold; color: #2962ff;">Bar ${params[0].axisValue}</div>`

        if (isBenchmark) {
          params.forEach((item: any) => {
            const value = typeof item.value === 'number' ? item.value.toFixed(2) : item.value
            result += `<div>${item.marker} ${item.seriesName}: <span style="color: ${item.color}; font-weight: bold;">${value}%</span></div>`
          })
          return result
        }

        // worst/best 模式：显示前 5 条路径 + 详情
        const shown = params.slice(0, 5)
        shown.forEach((item: any) => {
          const value = typeof item.value === 'number' ? item.value.toFixed(2) : item.value
          const pd = item.series?._pathDetail
          if (pd) {
            result += `<div style="margin-top: 4px; border-top: 1px solid #2a3449; padding-top: 4px;">`
            result += `<div>${item.marker} <b>${item.seriesName}</b>: <span style="color: ${item.color}; font-weight: bold;">${value}%</span></div>`
            result += `<div style="font-size: 11px; color: #a0aec0; margin-left: 16px;">`
            result += `收益率: ${(pd.total_return * 100).toFixed(1)}% | 回撤: ${(pd.max_drawdown * 100).toFixed(1)}% | 胜率: ${(pd.win_rate * 100).toFixed(0)}%`
            if (pd.longest_loss_streak > 0) result += ` | 最长连亏: ${pd.longest_loss_streak}天`
            if (pd.longest_win_streak > 0) result += ` | 最长连胜: ${pd.longest_win_streak}天`
            result += `</div></div>`
          } else {
            result += `<div>${item.marker} ${item.seriesName}: <span style="color: ${item.color}; font-weight: bold;">${value}%</span></div>`
          }
        })

        if (params.length > 5) {
          result += `<div style="color: #a0aec0; font-size: 11px; margin-top: 4px;">... 及 ${params.length - 5} 条路径</div>`
        }

        return result
      },
    },
    legend: {
      data: series.map((s: any) => s.name),
      textStyle: { color: '#e0e0e0', fontSize: 11 },
      top: isBenchmark ? 10 : undefined,
      show: isBenchmark,
    },
    grid: {
      left: '3%',
      right: '4%',
      bottom: '3%',
      top: isBenchmark ? '15%' : '5%',
      containLabel: true,
    },
    xAxis: {
      type: 'category',
      data: xLabels,
      axisLine: { lineStyle: { color: '#6E7079' } },
      axisLabel: {
        color: '#a0aec0',
        interval: Math.floor(xLabels.length / 10),
        formatter: (value: string) => `Day ${value}`,
      },
      name: 'Bar 索引',
      nameTextStyle: { color: '#a0aec0' },
    },
    yAxis: {
      type: 'value',
      axisLabel: {
        formatter: '{value}%',
        color: '#a0aec0',
      },
      axisLine: { lineStyle: { color: '#6E7079' } },
      splitLine: { lineStyle: { color: '#2a3449', type: 'dashed' } },
    },
    series,
  })
}
