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
  const { alpha, f_alpha } = props.data.multifractal_spectrum
  if (!alpha.length) return

  // 按 alpha 排序
  const pairs = alpha.map((a, i) => [a, f_alpha[i]])
    .filter(p => isFinite(p[0]) && isFinite(p[1]))
    .sort((a, b) => a[0] - b[0])

  const alphaMin = pairs[0][0]
  const alphaMax = pairs[pairs.length - 1][0]

  chart.setOption({
    title: {
      text: '多分形谱 f(α)',
      subtext: `Δα = ${props.data.width.toFixed(3)}`,
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
        return `α = ${p.data[0].toFixed(4)}<br/>f(α) = ${p.data[1].toFixed(4)}`
      }
    },
    grid: { left: 60, right: 30, top: 60, bottom: 40 },
    xAxis: {
      type: 'value',
      name: 'α',
      nameTextStyle: { color: '#999' },
      axisLine: { lineStyle: { color: '#444' } },
      axisLabel: { color: '#999' },
      splitLine: { lineStyle: { color: 'rgba(255,255,255,0.05)' } }
    },
    yAxis: {
      type: 'value',
      name: 'f(α)',
      nameTextStyle: { color: '#999' },
      axisLine: { lineStyle: { color: '#444' } },
      axisLabel: { color: '#999' },
      splitLine: { lineStyle: { color: 'rgba(255,255,255,0.05)' } }
    },
    series: [
      {
        type: 'line',
        data: pairs,
        smooth: true,
        symbol: 'circle',
        symbolSize: 4,
        lineStyle: { color: '#2962ff', width: 2 },
        itemStyle: { color: '#2962ff' },
        areaStyle: {
          color: new echarts.graphic.LinearGradient(0, 0, 0, 1, [
            { offset: 0, color: 'rgba(41,98,255,0.3)' },
            { offset: 1, color: 'rgba(41,98,255,0.02)' }
          ])
        },
        markArea: {
          silent: true,
          data: [[
            { xAxis: alphaMin, itemStyle: { color: 'rgba(41,98,255,0.08)' } },
            { xAxis: alphaMax }
          ]]
        },
        markPoint: {
          data: [
            { coord: [alphaMin, pairs[0][1]], name: 'α_min', symbol: 'circle', symbolSize: 8,
              itemStyle: { color: '#ff6d00' },
              label: { show: true, formatter: `α_min=${alphaMin.toFixed(3)}`, color: '#ff6d00', fontSize: 10, position: 'left' } },
            { coord: [alphaMax, pairs[pairs.length - 1][1]], name: 'α_max', symbol: 'circle', symbolSize: 8,
              itemStyle: { color: '#ff6d00' },
              label: { show: true, formatter: `α_max=${alphaMax.toFixed(3)}`, color: '#ff6d00', fontSize: 10, position: 'right' } }
          ]
        }
      }
    ]
  })
}

onMounted(() => {
  if (!chartRef.value) return
  chart = echarts.init(chartRef.value, undefined, { renderer: 'canvas' })
  render()
  window.addEventListener('resize', () => chart?.resize())
})

watch(() => props.data, render, { deep: true })

onBeforeUnmount(() => {
  chart?.dispose()
  chart = null
})
</script>

<style scoped>
.chart-container { width: 100%; height: 100%; min-height: 300px; }
</style>
