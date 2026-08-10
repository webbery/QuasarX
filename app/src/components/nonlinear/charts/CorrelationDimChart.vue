<template>
  <div ref="chartRef" class="chart-container"></div>
</template>

<script setup lang="ts">
import { ref, watch, onMounted, onBeforeUnmount } from 'vue'
import * as echarts from 'echarts'
import type { PhaseSpaceData } from '../composables/useNonlinearState'

const props = defineProps<{ data: PhaseSpaceData | null }>()
const chartRef = ref<HTMLDivElement>()
let chart: echarts.ECharts | null = null

function render() {
  if (!chart || !props.data) return
  const { corr_r_values, corr_c_values, correlation_dimension } = props.data
  if (!corr_r_values.length) return

  const scatterData = corr_r_values.map((r, i) => [r, corr_c_values[i]])

  // 线性拟合线
  const n = corr_r_values.length
  let sx = 0, sy = 0, sxx = 0, sxy = 0
  for (let i = 0; i < n; i++) {
    sx += corr_r_values[i]; sy += corr_c_values[i]
    sxx += corr_r_values[i] ** 2; sxy += corr_r_values[i] * corr_c_values[i]
  }
  const slope = (n * sxy - sx * sy) / (n * sxx - sx * sx)
  const intercept = (sy - slope * sx) / n
  const lineData = corr_r_values.map(r => [r, slope * r + intercept])

  chart.setOption({
    title: {
      text: '关联维数 D₂',
      subtext: `D₂ = ${correlation_dimension.toFixed(3)}`,
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
        return `ln(r) = ${p.data[0].toFixed(3)}<br/>ln C(r) = ${p.data[1].toFixed(3)}`
      }
    },
    legend: {
      data: ['ln C(r)', '线性拟合'],
      textStyle: { color: '#999' },
      top: 35
    },
    grid: { left: 60, right: 30, top: 65, bottom: 40 },
    xAxis: {
      type: 'value',
      name: 'ln(r)',
      nameTextStyle: { color: '#999' },
      axisLine: { lineStyle: { color: '#444' } },
      axisLabel: { color: '#999' },
      splitLine: { lineStyle: { color: 'rgba(255,255,255,0.05)' } }
    },
    yAxis: {
      type: 'value',
      name: 'ln C(r)',
      nameTextStyle: { color: '#999' },
      axisLine: { lineStyle: { color: '#444' } },
      axisLabel: { color: '#999' },
      splitLine: { lineStyle: { color: 'rgba(255,255,255,0.05)' } }
    },
    series: [
      {
        name: 'ln C(r)',
        type: 'scatter',
        data: scatterData,
        symbolSize: 5,
        itemStyle: { color: '#a0aec0' }
      },
      {
        name: '线性拟合',
        type: 'line',
        data: lineData,
        symbol: 'none',
        lineStyle: { color: '#ff6d00', width: 2, type: 'dashed' }
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
