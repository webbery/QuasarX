<template>
  <div ref="chartEl" class="chart-container"></div>
</template>

<script setup lang="ts">
import { onMounted, onBeforeUnmount, watch, ref, computed } from 'vue'
import * as echarts from 'echarts'
import type { Prediction } from '../composables/useXGBoostState'

const props = defineProps<{ predictions: Prediction[]; objective?: string }>()

let chart: echarts.ECharts | null = null
const chartEl = ref<HTMLDivElement | null>(null)

interface RocSeries {
  name: string
  points: { fpr: number; tpr: number }[]
  auc: number
  color: string
}

function computeRoc(predictions: Prediction[]): RocSeries[] {
  const actuals = new Set(predictions.map(p => Number(p.actual)))
  const classes = Array.from(actuals).sort((a, b) => a - b)
  const isBinary = classes.length === 2 && classes[0] === 0 && classes[1] === 1
  if (isBinary) {
    return [singleClassRoc(predictions, 1, '#5470c6', 'ROC (class=1)')]
  }
  if (classes.length <= 1) {
    return [{ name: 'ROC', points: [{ fpr: 0, tpr: 0 }, { fpr: 1, tpr: 1 }], auc: 0.5, color: '#64748b' }]
  }
  const palette = ['#5470c6', '#26a65b', '#ee6666', '#fac858', '#73c0de']
  return classes.map((cls, idx) =>
    singleClassRoc(predictions, cls, palette[idx % palette.length], `class=${cls}`),
  )
}

function singleClassRoc(predictions: Prediction[], positiveClass: number, color: string, name: string): RocSeries {
  const pairs = predictions
    .map(p => ({ y: Number(p.actual) === positiveClass ? 1 : 0, score: Number(p.predicted) }))
    .sort((a, b) => b.score - a.score)
  const nPos = pairs.filter(p => p.y === 1).length
  const nNeg = pairs.length - nPos
  if (nPos === 0 || nNeg === 0) {
    return { name, points: [{ fpr: 0, tpr: 0 }, { fpr: 1, tpr: 1 }], auc: 0.5, color }
  }
  let tp = 0, fp = 0
  const points = [{ fpr: 0, tpr: 0 }]
  for (const p of pairs) {
    if (p.y === 1) tp++
    else fp++
    points.push({ fpr: fp / nNeg, tpr: tp / nPos })
  }
  let auc = 0
  for (let i = 1; i < points.length; ++i) {
    const dx = points[i].fpr - points[i - 1].fpr
    const avgY = (points[i].tpr + points[i - 1].tpr) / 2
    auc += dx * avgY
  }
  return { name, points, auc: Number(auc.toFixed(4)), color }
}

function render() {
  if (!chart) return
  const series = computeRoc(props.predictions)
  const macroAuc = (series.reduce((s, r) => s + r.auc, 0) / series.length).toFixed(4)
  const titleText = series.length === 1
    ? `ROC 曲线 (AUC=${series[0].auc})`
    : `ROC 曲线 (macro AUC=${macroAuc})`
  const opts = {
    backgroundColor: 'transparent',
    title: {
      text: titleText,
      left: 'center',
      textStyle: { color: '#e2e8f0', fontSize: 13 },
    },
    tooltip: { trigger: 'axis' },
    legend: { data: series.map(s => s.name), top: 28, textStyle: { color: '#94a3b8', fontSize: 11 } },
    grid: { left: 50, right: 20, top: series.length > 1 ? 60 : 50, bottom: 40 },
    xAxis: { type: 'value', name: 'FPR', min: 0, max: 1, nameTextStyle: { color: '#94a3b8' }, axisLabel: { color: '#94a3b8' }, splitLine: { lineStyle: { color: 'rgba(255,255,255,0.05)' } } },
    yAxis: { type: 'value', name: 'TPR', min: 0, max: 1, nameTextStyle: { color: '#94a3b8' }, axisLabel: { color: '#94a3b8' }, splitLine: { lineStyle: { color: 'rgba(255,255,255,0.05)' } } },
    series: [
      ...series.map(s => ({
        name: s.name,
        type: 'line',
        showSymbol: false,
        smooth: false,
        data: s.points.map(p => [p.fpr, p.tpr]),
        itemStyle: { color: s.color },
        lineStyle: { width: 1.6 },
        areaStyle: { color: s.color + '33' },
      })),
      {
        name: 'Random',
        type: 'line',
        showSymbol: false,
        data: [[0, 0], [1, 1]],
        lineStyle: { type: 'dashed', color: '#64748b' },
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
watch(() => [props.predictions, props.objective], render, { deep: true })
</script>

<style scoped>
.chart-container { width: 100%; height: 320px; }
</style>
