<template>
  <div ref="chartEl" class="chart-container"></div>
</template>

<script setup lang="ts">
import { onMounted, onBeforeUnmount, watch, ref } from 'vue'
import * as echarts from 'echarts'
import type { Prediction } from '../composables/useXGBoostState'

const props = defineProps<{ predictions: Prediction[] }>()

let chart: echarts.ECharts | null = null
const chartEl = ref<HTMLDivElement | null>(null)

function render() {
  if (!chart) return
  const residuals = props.predictions.map(p => Number(p.predicted) - Number(p.actual))
  const opts = {
    title: { text: '残差分布', left: 'center' },
    tooltip: { trigger: 'axis' },
    grid: { left: 50, right: 30, top: 60, bottom: 40 },
    xAxis: { type: 'value', name: 'residual' },
    yAxis: { type: 'value', name: '频次' },
    series: [{
      name: 'residual',
      type: 'bar',
      data: bin(residuals, 30),
      itemStyle: { color: '#73c0de', opacity: 0.7 },
    }],
  }
  chart.setOption(opts, true)
}

function bin(values: number[], bins: number): ([number, number, number])[] {
  if (values.length === 0) return []
  const min = Math.min(...values)
  const max = Math.max(...values)
  const range = Math.max(max - min, 1e-9)
  const counts = new Array(bins).fill(0)
  for (const v of values) {
    const idx = Math.min(bins - 1, Math.max(0, Math.floor((v - min) / range * bins)))
    counts[idx]++
  }
  const edges = Array.from({ length: bins + 1 }, (_, i) => min + (range * i) / bins)
  return counts.map((c, i) => [(edges[i] + edges[i + 1]) / 2, c, edges[i]])
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
watch(() => props.predictions, render, { deep: true })
</script>

<style scoped>
.chart-container { width: 100%; height: 320px; }
</style>
