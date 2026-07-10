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

function computeRoc(predictions: Prediction[]) {
  // Compute ROC points from predictions
  const pairs = predictions
    .map(p => ({ y: Number(p.actual), score: Number(p.predicted) }))
    .sort((a, b) => b.score - a.score)
  const nPos = pairs.filter(p => p.y === 1).length
  const nNeg = pairs.length - nPos
  if (nPos === 0 || nNeg === 0) return { points: [{ fpr: 0, tpr: 0 }, { fpr: 1, tpr: 1 }], auc: 0.5 }

  let tp = 0, fp = 0
  const points: { fpr: number; tpr: number }[] = [{ fpr: 0, tpr: 0 }]
  for (const p of pairs) {
    if (p.y === 1) tp++
    else fp++
    points.push({ fpr: fp / nNeg, tpr: tp / nPos })
  }
  // Compute AUC via trapezoidal rule
  let auc = 0
  for (let i = 1; i < points.length; ++i) {
    const dx = points[i].fpr - points[i - 1].fpr
    const avgY = (points[i].tpr + points[i - 1].tpr) / 2
    auc += dx * avgY
  }
  return { points, auc: Number(auc.toFixed(4)) }
}

function render() {
  if (!chart) return
  const { points, auc } = computeRoc(props.predictions)
  const opts = {
    title: { text: `ROC 曲线 (AUC=${auc})`, left: 'center' },
    tooltip: { trigger: 'axis' },
    grid: { left: 60, right: 30, top: 70, bottom: 50 },
    xAxis: { type: 'value', name: 'FPR', min: 0, max: 1 },
    yAxis: { type: 'value', name: 'TPR', min: 0, max: 1 },
    series: [
      {
        name: 'ROC',
        type: 'line',
        showSymbol: false,
        smooth: false,
        data: points.map(p => [p.fpr, p.tpr]),
        itemStyle: { color: '#5470c6' },
        areaStyle: { color: 'rgba(84,112,198,0.2)' },
      },
      {
        name: 'Random',
        type: 'line',
        showSymbol: false,
        data: [[0, 0], [1, 1]],
        lineStyle: { type: 'dashed', color: '#999' },
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
watch(() => props.predictions, render, { deep: true })
</script>

<style scoped>
.chart-container { width: 100%; height: 320px; }
</style>
