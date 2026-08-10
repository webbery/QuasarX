<template>
  <div ref="chartRef" class="chart-container"></div>
</template>

<script setup lang="ts">
import { ref, watch, onMounted, onBeforeUnmount } from 'vue'
import * as echarts from 'echarts'
import type { MMARData } from '../composables/useNonlinearState'

const props = defineProps<{ data: MMARData | null }>()
const chartRef = ref<HTMLDivElement>()
let chart: echarts.ECharts | null = null

function render() {
  if (!chart || !props.data) return
  const { q_values, hq } = props.data

  const points = q_values.map((q, i) => [q, hq[i]])

  chart.setOption({
    title: {
      text: '广义 Hurst 指数 h(q)',
      subtext: `Hurst h(2) = ${props.data.hurst.toFixed(4)}`,
      textStyle: { color: '#e0e0e0', fontSize: 13 },
      subtextStyle: { color: '#999', fontSize: 11 }
    },
    tooltip: {
      trigger: 'axis',
      backgroundColor: 'rgba(26,34,54,0.95)',
      borderColor: '#2a3449',
      textStyle: { color: '#e0e0e0' },
      formatter: (params: any) => {
        const p = params[0]
        return `q = ${p.data[0].toFixed(1)}<br/>h(q) = ${p.data[1].toFixed(4)}`
      }
    },
    grid: { left: 60, right: 30, top: 60, bottom: 40 },
    xAxis: {
      type: 'value',
      name: 'q',
      nameTextStyle: { color: '#999' },
      axisLine: { lineStyle: { color: '#444' } },
      axisLabel: { color: '#999' },
      splitLine: { lineStyle: { color: 'rgba(255,255,255,0.05)' } }
    },
    yAxis: {
      type: 'value',
      name: 'h(q)',
      nameTextStyle: { color: '#999' },
      axisLine: { lineStyle: { color: '#444' } },
      axisLabel: { color: '#999' },
      splitLine: { lineStyle: { color: 'rgba(255,255,255,0.05)' } }
    },
    series: [
      {
        type: 'line',
        data: points,
        smooth: false,
        symbol: 'circle',
        symbolSize: 5,
        lineStyle: { color: '#00c853', width: 2 },
        itemStyle: { color: '#00c853' }
      },
      // h=0.5 参考线（随机游走）
      {
        type: 'line',
        markLine: {
          silent: true,
          symbol: 'none',
          lineStyle: { color: '#ff6d00', type: 'dashed', width: 1 },
          data: [{ yAxis: 0.5 }],
          label: {
            formatter: 'h=0.5 (随机游走)',
            color: '#ff6d00',
            fontSize: 10
          }
        },
        data: []
      }
    ]
  })
}

onMounted(() => {
  if (!chartRef.value) return
  chart = echarts.init(chartRef.value)
  render()
  window.addEventListener('resize', () => chart?.resize())
})

watch(() => props.data, render, { deep: true })

onBeforeUnmount(() => { chart?.dispose(); chart = null })
</script>

<style scoped>
.chart-container { width: 100%; height: 100%; min-height: 300px; }
</style>
