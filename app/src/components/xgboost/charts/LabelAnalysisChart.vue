<template>
  <div ref="chartEl" class="chart-container"></div>
</template>

<script setup lang="ts">
import { ref, onMounted, onBeforeUnmount, watch } from 'vue'
import * as echarts from 'echarts'
import type { LabelAnalysisResult } from '../composables/useXGBoostState'

const props = defineProps<{ data: LabelAnalysisResult }>()

let chart: echarts.ECharts | null = null
const chartEl = ref<HTMLDivElement | null>(null)

const LABEL_NAMES: Record<number, string> = { 0: 'UP (看涨)', 1: 'FLAT (震荡)', 2: 'DOWN (看跌)' }
const LABEL_COLORS: Record<number, string> = { 0: 'rgba(38, 166, 91, 0.15)', 1: 'rgba(158, 158, 158, 0.10)', 2: 'rgba(234, 57, 67, 0.15)' }

function buildMarkAreaData(): [any, any][] {
  const { dates, labels } = props.data
  const areas: [any, any][] = []
  let i = 0
  while (i < dates.length) {
    if (labels[i] < 0) { i++; continue }
    const start = i
    while (i < dates.length && labels[i] === labels[start]) i++
    const end = i - 1
    const color = LABEL_COLORS[labels[start]] || LABEL_COLORS[1]
    areas.push([
      { xAxis: dates[start], itemStyle: { color } },
      { xAxis: dates[end] },
    ])
  }
  return areas
}

function render() {
  if (!chart) return
  const { dates, prices, labels, symbol, field, threshold } = props.data

  const fieldNames: Record<string, string> = {
    close: '收盘价', open: '开盘价', high: '最高价', low: '最低价', volume: '成交量',
  }

  const tooltipData = dates.map((d, i) => ({
    date: d,
    price: prices[i],
    label: labels[i] >= 0 ? (LABEL_NAMES[labels[i]] || 'N/A') : '数据不足',
  }))

  const opts: echarts.EChartsOption = {
    backgroundColor: 'transparent',
    title: {
      text: `${symbol} ${fieldNames[field] || field} 标签分析`,
      subtext: `阈值: ${(threshold * 100).toFixed(2)}%  |  标签周期: N`,
      left: 'center',
      textStyle: { color: '#e0e0e0', fontSize: 14 },
      subtextStyle: { color: '#999', fontSize: 11 },
    },
    tooltip: {
      trigger: 'axis',
      backgroundColor: 'rgba(30,42,58,0.95)',
      borderColor: '#2b3a55',
      textStyle: { color: '#e0e0e0', fontSize: 12 },
      formatter: (params: any) => {
        const idx = params[0]?.dataIndex
        if (idx == null || idx >= tooltipData.length) return ''
        const d = tooltipData[idx]
        const labelColor = labels[idx] === 0 ? '#26a65b' : labels[idx] === 2 ? '#ea3943' : '#9e9e9e'
        return `<b>${d.date}</b><br/>` +
          `${fieldNames[field] || field}: ${d.price.toFixed(2)}<br/>` +
          `标签: <span style="color:${labelColor};font-weight:600">${d.label}</span>`
      },
    },
    legend: {
      data: [fieldNames[field] || field],
      top: 50,
      textStyle: { color: '#999' },
    },
    grid: { left: 60, right: 30, top: 85, bottom: 40 },
    xAxis: {
      type: 'category',
      data: dates,
      axisLine: { lineStyle: { color: '#444' } },
      axisLabel: { color: '#999', fontSize: 10 },
    },
    yAxis: {
      type: 'value',
      scale: true,
      axisLine: { lineStyle: { color: '#444' } },
      axisLabel: { color: '#999' },
      splitLine: { lineStyle: { color: 'rgba(255,255,255,0.05)' } },
    },
    series: [
      {
        name: fieldNames[field] || field,
        type: 'line',
        data: prices,
        showSymbol: false,
        lineStyle: { width: 1.5, color: '#5b8ff9' },
        itemStyle: { color: '#5b8ff9' },
        markArea: {
          silent: true,
          data: buildMarkAreaData(),
        },
      },
    ],
  }
  chart.setOption(opts, true)
}

onMounted(() => {
  if (chartEl.value) {
    chart = echarts.init(chartEl.value)
    render()
  }
  window.addEventListener('resize', handleResize)
})

onBeforeUnmount(() => {
  window.removeEventListener('resize', handleResize)
  chart?.dispose()
  chart = null
})

function handleResize() { chart?.resize() }

watch(() => props.data, render, { deep: true })
</script>

<style scoped>
.chart-container { width: 100%; height: 380px; }
</style>
