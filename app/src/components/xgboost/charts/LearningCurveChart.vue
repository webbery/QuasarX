<template>
  <div ref="chartEl" class="chart-container"></div>
</template>

<script setup lang="ts">
import { onMounted, onBeforeUnmount, watch } from 'vue'
import * as echarts from 'echarts'
import type { LearningCurvePoint } from '../composables/useXGBoostState'

const props = defineProps<{ data: LearningCurvePoint[] }>()

let chart: echarts.ECharts | null = null
const chartEl = ref<HTMLDivElement | null>(null)

function render() {
  if (!chart) return
  const opts = {
    title: { text: '学习曲线', left: 'center' },
    tooltip: { trigger: 'axis' },
    legend: { data: ['train_loss', 'eval_loss'], top: 24 },
    grid: { left: 50, right: 24, top: 70, bottom: 40 },
    xAxis: {
      type: 'category',
      data: props.data.map(d => d.iteration),
      name: 'Iteration',
    },
    yAxis: { type: 'value', name: 'Loss', scale: true },
    series: [
      {
        name: 'train_loss',
        type: 'line',
        smooth: true,
        showSymbol: false,
        data: props.data.map(d => Number(d.train_loss.toFixed(4))),
        itemStyle: { color: '#5470c6' },
      },
      {
        name: 'eval_loss',
        type: 'line',
        smooth: true,
        showSymbol: false,
        data: props.data.map(d => Number(d.eval_loss.toFixed(4))),
        itemStyle: { color: '#ee6666' },
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

import { ref } from 'vue'
</script>

<style scoped>
.chart-container { width: 100%; height: 320px; }
</style>
