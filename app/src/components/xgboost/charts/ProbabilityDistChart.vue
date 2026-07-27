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
  const posScores = props.predictions.filter(p => p.actual === 1).map(p => Number(p.predicted))
  const negScores = props.predictions.filter(p => p.actual === 0).map(p => Number(p.predicted))
  const opts = {
    backgroundColor: 'transparent',
    title: { text: '正/负样本预测概率分布', left: 'center', textStyle: { color: '#e2e8f0', fontSize: 13 } },
    tooltip: { trigger: 'axis', backgroundColor: 'rgba(15,25,41,0.95)', borderColor: '#2b3a55', textStyle: { color: '#e0e0e0' } },
    legend: { data: ['正样本 (actual=1)', '负样本 (actual=0)'], top: 24, textStyle: { color: '#94a3b8' } },
    grid: { left: 50, right: 30, top: 70, bottom: 40 },
    xAxis: { type: 'value', name: '预测概率', min: 0, max: 1, nameTextStyle: { color: '#94a3b8' }, axisLabel: { color: '#94a3b8' }, splitLine: { lineStyle: { color: 'rgba(255,255,255,0.05)' } } },
    yAxis: { type: 'value', name: '频次', nameTextStyle: { color: '#94a3b8' }, axisLabel: { color: '#94a3b8' }, splitLine: { lineStyle: { color: 'rgba(255,255,255,0.05)' } } },
    series: [
      {
        name: '正样本 (actual=1)',
        type: 'bar',
        data: bin(posScores, 20),
        itemStyle: { color: '#ee6666', opacity: 0.6 },
      },
      {
        name: '负样本 (actual=0)',
        type: 'bar',
        data: bin(negScores, 20),
        itemStyle: { color: '#5470c6', opacity: 0.6 },
      },
    ],
  }
  chart.setOption(opts, true)
}

function bin(values: number[], bins: number): ([number, number, number])[] {
  const edges = Array.from({ length: bins + 1 }, (_, i) => i / bins)
  const counts = new Array(bins).fill(0)
  for (const v of values) {
    if (v == null || isNaN(v)) continue
    const idx = Math.min(bins - 1, Math.max(0, Math.floor(v * bins)))
    counts[idx]++
  }
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
