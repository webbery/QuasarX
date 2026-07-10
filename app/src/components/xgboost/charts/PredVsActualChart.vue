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
  const points = props.predictions.map(p => [Number(p.actual), Number(p.predicted)])
  const minV = Math.min(...points.flat())
  const maxV = Math.max(...points.flat())

  const opts = {
    title: { text: '预测 vs 实际', left: 'center' },
    tooltip: {
      trigger: 'item',
      formatter: (p: any) => `Actual=${p.data[0].toFixed(4)}<br/>Pred=${p.data[1].toFixed(4)}`,
    },
    grid: { left: 60, right: 30, top: 60, bottom: 50 },
    xAxis: { type: 'value', name: 'Actual', min: minV, max: maxV },
    yAxis: { type: 'value', name: 'Predicted', min: minV, max: maxV },
    series: [
      {
        name: 'Pred vs Actual',
        type: 'scatter',
        symbolSize: 6,
        data: points,
        itemStyle: { color: '#5470c6', opacity: 0.5 },
      },
      {
        name: '45°',
        type: 'line',
        showSymbol: false,
        data: [[minV, minV], [maxV, maxV]],
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
