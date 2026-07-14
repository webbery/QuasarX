<template>
  <div class="correlation-scatter" ref="chartRef" style="width: 100%; height: 100%"></div>
</template>

<script setup lang="ts">
import { watch, onMounted } from 'vue'
import { useECharts, createBaseChartOption } from '../../report/composables/useECharts'
import type { AlignedData, PriceBar } from '../composables/useFundamentalState'

const props = defineProps<{
  aligned: AlignedData | null
  prices: PriceBar[]
  yDays: number  // 未来 N 日收益率
}>()

const { chartRef, initChart, updateChart } = useECharts(false)
onMounted(() => initChart())

watch(() => [props.aligned, props.yDays] as const, () => {
  if (!props.aligned || props.prices.length === 0) return

  const { profitQuarters, dates } = props.aligned
  if (profitQuarters.length === 0) return

  // 构建散点数据: X = ROE 环比变化, Y = 未来 N 日收益率
  const scatterData: [number, number, string][] = []

  for (let i = 1; i < profitQuarters.length; i++) {
    const curr = profitQuarters[i]
    const prev = profitQuarters[i - 1]

    if (curr.roe_avg == null || prev.roe_avg == null) continue

    // X: ROE 环比变化
    const roeChange = curr.roe_avg - prev.roe_avg

    // Y: 财报发布后 N 日收益率
    const pubDate = curr.pub_date || curr.stat_date
    const futureReturn = calcFutureReturn(props.prices, pubDate, props.yDays)
    if (futureReturn == null) continue

    scatterData.push([roeChange * 100, futureReturn * 100, curr.stat_date])
  }

  if (scatterData.length === 0) return

  // 线性回归
  const { slope, intercept, r2 } = linearRegression(scatterData.map(d => [d[0], d[1]]))
  const xMin = Math.min(...scatterData.map(d => d[0]))
  const xMax = Math.max(...scatterData.map(d => d[0]))
  const regressionLine: [number, number][] = [
    [xMin, slope * xMin + intercept],
    [xMax, slope * xMax + intercept],
  ]

  const option = createBaseChartOption({
    title: { text: `ROE 变化 vs 未来 ${props.yDays} 日收益`, left: 'center', textStyle: { color: '#e0e0e0', fontSize: 13 } },
    tooltip: {
      formatter: (params: any) => {
        if (params.seriesType === 'scatter') {
          const [x, y, date] = params.data
          return `${date}<br/>ROE 变化: ${x.toFixed(2)}%<br/>未来收益: ${y.toFixed(2)}%`
        }
        return ''
      },
    },
    grid: { left: 60, right: 30, top: 50, bottom: 50 },
    xAxis: {
      type: 'value',
      name: 'ROE 环比变化 (%)',
      nameTextStyle: { color: '#a0aec0' },
      axisLabel: { color: '#a0aec0' },
      splitLine: { lineStyle: { color: '#333' } },
    },
    yAxis: {
      type: 'value',
      name: `未来 ${props.yDays} 日收益 (%)`,
      nameTextStyle: { color: '#a0aec0' },
      axisLabel: { color: '#a0aec0' },
      splitLine: { lineStyle: { color: '#333' } },
    },
    series: [
      {
        type: 'scatter',
        data: scatterData,
        symbolSize: 10,
        itemStyle: { color: '#2962ff', opacity: 0.7 },
      },
      {
        type: 'line',
        data: regressionLine,
        symbol: 'none',
        lineStyle: { color: '#ff9800', type: 'dashed', width: 1 },
        tooltip: { show: false },
      },
    ],
    graphic: [{
      type: 'text',
      right: 40,
      top: 55,
      style: {
        text: `R² = ${r2.toFixed(3)}`,
        fill: '#a0aec0',
        fontSize: 11,
      },
    }],
  })

  updateChart(option)
}, { immediate: true, deep: true })

function calcFutureReturn(prices: PriceBar[], startDate: string, days: number): number | null {
  const startIdx = prices.findIndex(p => p.datetime >= startDate)
  if (startIdx < 0) return null
  const endIdx = Math.min(startIdx + days, prices.length - 1)
  if (endIdx <= startIdx) return null
  return (prices[endIdx].close - prices[startIdx].close) / prices[startIdx].close
}

function linearRegression(points: [number, number][]): { slope: number; intercept: number; r2: number } {
  const n = points.length
  if (n < 2) return { slope: 0, intercept: 0, r2: 0 }

  let sumX = 0, sumY = 0, sumXY = 0, sumX2 = 0, sumY2 = 0
  for (const [x, y] of points) {
    sumX += x; sumY += y; sumXY += x * y; sumX2 += x * x; sumY2 += y * y
  }

  const denom = n * sumX2 - sumX * sumX
  if (denom === 0) return { slope: 0, intercept: sumY / n, r2: 0 }

  const slope = (n * sumXY - sumX * sumY) / denom
  const intercept = (sumY - slope * sumX) / n

  // R²
  const yMean = sumY / n
  const ssTot = sumY2 - n * yMean * yMean
  const ssRes = points.reduce((acc, [x, y]) => acc + (y - (slope * x + intercept)) ** 2, 0)
  const r2 = ssTot > 0 ? 1 - ssRes / ssTot : 0

  return { slope, intercept, r2 }
}
</script>
