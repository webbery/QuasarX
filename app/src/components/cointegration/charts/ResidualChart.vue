<template>
  <div class="residual-chart" ref="chartRef" :style="{ height: height + 'px' }"></div>
</template>

<script setup lang="ts">
import { ref, onMounted, watch } from 'vue'
import * as echarts from 'echarts'
import type { EGFullResult } from '../composables/useCointegrationState'

const props = defineProps<{
  data: EGFullResult
  height?: number
}>()

const chartRef = ref<HTMLElement>()
let chart: echarts.ECharts | null = null

function render() {
  if (!chartRef.value || !props.data) return
  if (!chart) chart = echarts.init(chartRef.value)

  const residuals = props.data.residuals
  const xData = residuals.map((_, i) => i)

  // ADF 标注
  const adfStat = props.data.adf.statistic.toFixed(3)
  const adfP = props.data.adf.p_value.toFixed(4)
  const kpssStat = props.data.kpss.statistic.toFixed(3)
  const kpssP = props.data.kpss.p_value.toFixed(4)

  chart.setOption({
    backgroundColor: 'transparent',
    title: {
      text: `协整残差: ${props.data.symbol_x} vs ${props.data.symbol_y}`,
      subtext: `ADF=${adfStat} (p=${adfP}) | KPSS=${kpssStat} (p=${kpssP}) | β=${props.data.beta.toFixed(4)}`,
      textStyle: { color: '#ccc', fontSize: 14 },
      subtextStyle: { color: '#999', fontSize: 11 },
      left: 'center',
    },
    tooltip: { trigger: 'axis' },
    grid: [
      { left: 60, right: 30, top: 60, height: '35%' },
      { left: 60, right: 30, top: '58%', height: '32%' },
    ],
    xAxis: [
      { type: 'category', data: xData, gridIndex: 0, show: false },
      { type: 'category', data: xData, gridIndex: 1, axisLabel: { color: '#888' } },
    ],
    yAxis: [
      { type: 'value', gridIndex: 0, name: 'Spread', axisLabel: { color: '#888' }, splitLine: { lineStyle: { color: '#222' } } },
      { type: 'value', gridIndex: 1, name: '残差 ε', axisLabel: { color: '#888' }, splitLine: { lineStyle: { color: '#222' } } },
    ],
    series: [
      {
        name: 'Spread (y - βx)',
        type: 'line',
        xAxisIndex: 0,
        yAxisIndex: 0,
        data: residuals.map((_, i) => {
          // 用残差 + 拟合值近似原始 spread
          return residuals[i]
        }),
        lineStyle: { width: 1, color: '#5c9eff' },
        symbol: 'none',
        areaStyle: { color: 'rgba(92,158,255,0.05)' },
      },
      {
        name: '残差',
        type: 'line',
        xAxisIndex: 1,
        yAxisIndex: 1,
        data: residuals,
        lineStyle: { width: 1, color: '#ff9800' },
        symbol: 'none',
        markLine: {
          silent: true,
          data: [{ yAxis: 0, lineStyle: { color: '#555', type: 'dashed' } }],
          label: { show: false },
        },
      },
    ],
  })
}

onMounted(() => {
  render()
  window.addEventListener('resize', () => chart?.resize())
})

watch(() => props.data, render, { deep: true })
</script>

<style scoped>
.residual-chart { width: 100%; min-height: 350px; }
</style>
