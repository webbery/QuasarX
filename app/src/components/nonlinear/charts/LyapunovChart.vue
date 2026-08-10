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
  const { lyap_divergence, max_lyapunov } = props.data
  if (!lyap_divergence.length) return

  const points = lyap_divergence
    .map((v, t) => [t, v])
    .filter(p => isFinite(p[1]))

  chart.setOption({
    title: {
      text: 'Lyapunov 指数估计',
      subtext: `λ_max = ${max_lyapunov.toFixed(4)}`,
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
        return `t = ${p.data[0]}<br/>ln d(t) = ${p.data[1].toFixed(4)}`
      }
    },
    grid: { left: 60, right: 30, top: 60, bottom: 40 },
    xAxis: {
      type: 'value',
      name: '时间步 t',
      nameTextStyle: { color: '#999' },
      axisLine: { lineStyle: { color: '#444' } },
      axisLabel: { color: '#999' },
      splitLine: { lineStyle: { color: 'rgba(255,255,255,0.05)' } }
    },
    yAxis: {
      type: 'value',
      name: '⟨ln d(t)⟩',
      nameTextStyle: { color: '#999' },
      axisLine: { lineStyle: { color: '#444' } },
      axisLabel: { color: '#999' },
      splitLine: { lineStyle: { color: 'rgba(255,255,255,0.05)' } }
    },
    series: [
      {
        type: 'line',
        data: points,
        smooth: true,
        symbol: 'none',
        lineStyle: { color: '#e040fb', width: 2 },
        areaStyle: {
          color: new echarts.graphic.LinearGradient(0, 0, 0, 1, [
            { offset: 0, color: 'rgba(224,64,251,0.2)' },
            { offset: 1, color: 'rgba(224,64,251,0.01)' }
          ])
        },
        markLine: {
          silent: true,
          symbol: 'none',
          lineStyle: { color: '#ff6d00', type: 'dashed', width: 1 },
          data: [{ yAxis: 0 }],
          label: { formatter: 'λ=0', color: '#ff6d00', fontSize: 10 }
        }
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
