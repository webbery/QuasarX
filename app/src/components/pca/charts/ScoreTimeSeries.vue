<template>
  <div class="score-timeseries">
    <h4>主成分得分时序 (Score Time Series)</h4>
    <div ref="chartRef" class="chart-container"></div>
  </div>
</template>

<script setup lang="ts">
import { ref, watch, onMounted, onBeforeUnmount } from 'vue'
import * as echarts from 'echarts'

const props = defineProps<{
  dates: string[]
  scores: number[][]
  nComponents: number
}>()

const chartRef = ref<HTMLElement>()
let chartInstance: echarts.EChartsType | null = null

const COLORS = ['#2962ff', '#ff9800', '#00c853', '#ef232a', '#9c27b0', '#00bcd4']

function buildOption() {
  if (!props.dates.length || !props.scores.length) return {}

  const { dates, scores, nComponents } = props
  const series = []

  for (let j = 0; j < nComponents; j++) {
    series.push({
      name: `PC${j + 1}`,
      type: 'line' as const,
      data: scores.map(row => row[j] ?? 0),
      smooth: false,
      showSymbol: false,
      lineStyle: { width: 2 },
      itemStyle: { color: COLORS[j % COLORS.length] }
    })
  }

  return {
    tooltip: {
      trigger: 'axis' as const,
      axisPointer: { type: 'cross' as const }
    },
    legend: {
      data: Array.from({ length: nComponents }, (_, i) => `PC${i + 1}`),
      textStyle: { color: '#e0e0e0' },
      top: 0
    },
    grid: { left: 50, right: 30, top: 40, bottom: 60 },
    xAxis: {
      type: 'category' as const,
      data: dates,
      axisLabel: {
        color: '#999',
        rotate: 45,
        formatter: (value: string) => {
          if (value && value.length >= 10) return value.substring(5)
          return value
        }
      },
      axisLine: { lineStyle: { color: '#555' } },
      splitLine: { show: false }
    },
    yAxis: {
      type: 'value' as const,
      name: '得分',
      axisLabel: { color: '#999' },
      axisLine: { lineStyle: { color: '#555' } },
      splitLine: { lineStyle: { color: 'rgba(74, 85, 104, 0.3)' } }
    },
    dataZoom: [
      {
        type: 'slider' as const,
        bottom: 10,
        height: 20,
        start: Math.max(0, 100 - 100 * 252 / dates.length),
        end: 100,
        textStyle: { color: '#999' },
        borderColor: '#555',
        fillerColor: 'rgba(41, 98, 255, 0.2)',
        handleStyle: { color: '#2962ff' }
      },
      { type: 'inside' as const }
    ],
    series
  }
}

function initChart() {
  if (!chartRef.value) return
  chartInstance = echarts.init(chartRef.value)
  chartInstance.setOption(buildOption())
}

function updateChart() {
  chartInstance?.setOption(buildOption(), true)
}

watch(() => props.scores, () => {
  if (props.scores.length > 0) {
    if (!chartInstance && chartRef.value) initChart()
    else if (chartInstance) updateChart()
  }
}, { immediate: true, deep: true })

onMounted(() => {
  window.addEventListener('resize', handleResize)
})

onBeforeUnmount(() => {
  window.removeEventListener('resize', handleResize)
  chartInstance?.dispose()
})

function handleResize() {
  chartInstance?.resize()
}
</script>

<style scoped>
.score-timeseries {
  background: rgba(26, 34, 54, 0.9);
  border-radius: 8px;
  padding: 16px;
}
.chart-container {
  width: 100%;
  height: 300px;
}
</style>
