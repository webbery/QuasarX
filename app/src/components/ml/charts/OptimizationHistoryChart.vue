<template>
  <div ref="chartRef" :style="{ width: '100%', height: height + 'px' }"></div>
</template>

<script setup lang="ts">
import { ref, watch, onMounted, onUnmounted, nextTick } from 'vue'
import * as echarts from 'echarts'

interface Trial {
  number: number
  value: number | null
  best: number | null
  status: string
}

const props = defineProps<{
  trials: Trial[]
  metric: string
  height?: number
}>()

const chartRef = ref<HTMLDivElement>()
let chart: echarts.ECharts | null = null

function render() {
  if (!chartRef.value || !props.trials.length) return
  if (!chart) chart = echarts.init(chartRef.value)

  const okTrials = props.trials.filter(t => t.status === 'ok' && t.value != null)
  const numbers = okTrials.map(t => t.number)
  const values = okTrials.map(t => t.value!)
  const bests = okTrials.map(t => t.best!)

  chart.setOption({
    tooltip: {
      trigger: 'axis',
      backgroundColor: '#1a1a2e',
      borderColor: '#333',
      textStyle: { color: '#e0e0e0', fontSize: 12 },
    },
    legend: {
      data: ['Trial Value', 'Best So Far'],
      textStyle: { color: '#999', fontSize: 11 },
      top: 0,
    },
    grid: { left: 50, right: 20, top: 35, bottom: 30 },
    xAxis: {
      type: 'category',
      data: numbers,
      name: 'Trial #',
      nameTextStyle: { color: '#888' },
      axisLine: { lineStyle: { color: '#444' } },
      axisLabel: { color: '#999' },
    },
    yAxis: {
      type: 'value',
      name: props.metric,
      nameTextStyle: { color: '#888' },
      axisLine: { lineStyle: { color: '#444' } },
      axisLabel: { color: '#999' },
      splitLine: { lineStyle: { color: '#2a2a3e' } },
    },
    series: [
      {
        name: 'Trial Value',
        type: 'scatter',
        data: values,
        symbolSize: 6,
        itemStyle: { color: '#5b8def', opacity: 0.7 },
      },
      {
        name: 'Best So Far',
        type: 'line',
        data: bests,
        smooth: true,
        lineStyle: { color: '#f5a623', width: 2 },
        itemStyle: { color: '#f5a623' },
        symbol: 'none',
      },
    ],
  })
}

watch(() => props.trials, render, { deep: true })
onMounted(() => nextTick(render))

const ro = ref<ResizeObserver>()
onMounted(() => {
  ro.value = new ResizeObserver(() => chart?.resize())
  if (chartRef.value) ro.value.observe(chartRef.value)
})
onUnmounted(() => {
  ro.value?.disconnect()
  chart?.dispose()
})
</script>
