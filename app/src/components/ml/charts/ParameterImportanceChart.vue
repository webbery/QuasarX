<template>
  <div ref="chartRef" :style="{ width: '100%', height: height + 'px' }"></div>
</template>

<script setup lang="ts">
import { ref, watch, onMounted, onUnmounted, nextTick } from 'vue'
import * as echarts from 'echarts'

const props = defineProps<{
  importance: { name: string; importance: number }[]
  height?: number
}>()

const chartRef = ref<HTMLDivElement>()
let chart: echarts.ECharts | null = null

function render() {
  if (!chartRef.value || !props.importance.length) return
  if (!chart) chart = echarts.init(chartRef.value)

  const sorted = [...props.importance].sort((a, b) => a.importance - b.importance)

  chart.setOption({
    tooltip: {
      trigger: 'axis',
      backgroundColor: '#1a1a2e',
      borderColor: '#333',
      textStyle: { color: '#e0e0e0', fontSize: 12 },
      formatter: (params: any) => {
        const p = params[0]
        return `${p.name}<br/>Importance: ${(p.value * 100).toFixed(1)}%`
      },
    },
    grid: { left: 120, right: 30, top: 10, bottom: 20 },
    xAxis: {
      type: 'value',
      max: 1,
      axisLabel: { color: '#999', formatter: (v: number) => `${(v * 100).toFixed(0)}%` },
      splitLine: { lineStyle: { color: '#2a2a3e' } },
    },
    yAxis: {
      type: 'category',
      data: sorted.map(i => i.name),
      axisLine: { lineStyle: { color: '#444' } },
      axisLabel: { color: '#ccc', fontSize: 11 },
    },
    series: [{
      type: 'bar',
      data: sorted.map(i => i.importance),
      barWidth: '60%',
      itemStyle: {
        color: new echarts.graphic.LinearGradient(0, 0, 1, 0, [
          { offset: 0, color: '#5b8def' },
          { offset: 1, color: '#8b5cf6' },
        ]),
        borderRadius: [0, 4, 4, 0],
      },
    }],
  })
}

watch(() => props.importance, render, { deep: true })
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
