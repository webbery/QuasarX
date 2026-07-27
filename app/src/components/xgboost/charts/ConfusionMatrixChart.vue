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

function computeMatrix(predictions: Prediction[]) {
  const m: Record<string, Record<string, number>> = {}
  for (const p of predictions) {
    const a = String(p.actual)
    const c = String(p.pred_class)
    if (!m[a]) m[a] = {}
    m[a][c] = (m[a][c] || 0) + 1
  }
  const labels = Array.from(new Set([...predictions.map(p => String(p.actual)),
                                     ...predictions.map(p => String(p.pred_class))])).sort()
  const data: { x: string; y: string; v: number }[] = []
  for (const a of labels) {
    for (const c of labels) {
      data.push({ y: a, x: c, v: m[a]?.[c] || 0 })
    }
  }
  return { labels, data }
}

function render() {
  if (!chart) return
  const { labels, data } = computeMatrix(props.predictions)
  const labelMap: Record<string, string> = { '0': 'UP', '1': 'FLAT', '2': 'DOWN' }
  const displayLabels = labels.map(l => labelMap[l] || l)
  const opts = {
    backgroundColor: 'transparent',
    title: { text: '混淆矩阵', left: 'center', textStyle: { color: '#e2e8f0', fontSize: 13 } },
    tooltip: {
      position: 'top',
      backgroundColor: 'rgba(15,25,41,0.95)',
      borderColor: '#2b3a55',
      textStyle: { color: '#e0e0e0' },
      formatter: (p: any) => `Actual=${labelMap[String(p.data[1])] || p.data[1]}<br/>Pred=${labelMap[String(p.data[0])] || p.data[0]}<br/>Count=${p.data[2]}`,
    },
    grid: { left: 80, right: 60, top: 50, bottom: 70 },
    xAxis: { type: 'category', data: displayLabels, name: 'Predicted', nameLocation: 'middle', nameGap: 28, nameTextStyle: { color: '#94a3b8' }, axisLabel: { color: '#cbd5e1' } },
    yAxis: { type: 'category', data: displayLabels, name: 'Actual', nameLocation: 'middle', nameGap: 36, nameTextStyle: { color: '#94a3b8' }, axisLabel: { color: '#cbd5e1' } },
    visualMap: {
      min: 0,
      max: Math.max(1, ...data.map(d => d.v)),
      calculable: true,
      orient: 'vertical',
      right: 0,
      top: 'middle',
      textStyle: { color: '#94a3b8' },
    },
    series: [{
      name: 'Count',
      type: 'heatmap',
      data: data.map(d => [displayLabels[labels.indexOf(d.x)], displayLabels[labels.indexOf(d.y)], d.v]),
      label: { show: true, color: '#f8fafc' },
    }],
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
watch(() => props.predictions, render, { deep: true })
</script>

<style scoped>
.chart-container { width: 100%; height: 320px; }
</style>
