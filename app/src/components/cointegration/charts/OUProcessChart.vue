<template>
  <div class="ou-chart" ref="chartRef" :style="{ height: height + 'px' }"></div>
</template>

<script setup lang="ts">
import { ref, onMounted, watch } from 'vue'
import * as echarts from 'echarts'
import type { OUProcessResult } from '../composables/useCointegrationState'

const props = defineProps<{
  ou: OUProcessResult
  residuals: number[]
  height?: number
}>()

const chartRef = ref<HTMLElement>()
let chart: echarts.ECharts | null = null

function render() {
  if (!chartRef.value || !props.ou) return
  if (!chart) chart = echarts.init(chartRef.value)

  const { theta, mu, sigma, half_life: hl } = props.ou
  const xData = props.residuals.map((_, i) => i)

  // OU 拟合路径: 从第一个残差开始, 用 OU 参数模拟均值回复路径
  const ouPath: number[] = [props.residuals[0]]
  for (let i = 1; i < props.residuals.length; i++) {
    const prev = ouPath[i - 1]
    const drift = theta * (mu - prev)
    ouPath.push(prev + drift)
  }

  chart.setOption({
    backgroundColor: 'transparent',
    title: {
      text: `OU 过程拟合`,
      subtext: `θ=${theta.toFixed(4)} | μ=${mu.toFixed(4)} | σ=${sigma.toFixed(4)} | 半衰期=${hl > 0 ? hl.toFixed(1) : '—'} | AIC=${props.ou.aic.toFixed(1)}`,
      textStyle: { color: '#ccc', fontSize: 14 },
      subtextStyle: { color: '#999', fontSize: 11 },
      left: 'center',
    },
    tooltip: { trigger: 'axis' },
    legend: { data: ['实际残差', 'OU 拟合路径', '长期均值 μ'], top: 50, textStyle: { color: '#aaa' } },
    grid: { left: 60, right: 30, top: 80, bottom: 30 },
    xAxis: { type: 'category', data: xData, axisLabel: { color: '#888' } },
    yAxis: { type: 'value', axisLabel: { color: '#888' }, splitLine: { lineStyle: { color: '#222' } } },
    series: [
      {
        name: '实际残差',
        type: 'line',
        data: props.residuals,
        lineStyle: { width: 1, color: '#ff9800' },
        symbol: 'none',
      },
      {
        name: 'OU 拟合路径',
        type: 'line',
        data: ouPath,
        lineStyle: { width: 2, color: '#4caf50' },
        symbol: 'none',
      },
      {
        name: '长期均值 μ',
        type: 'line',
        data: xData.map(() => mu),
        lineStyle: { width: 1, color: '#9c27b0', type: 'dashed' },
        symbol: 'none',
      },
    ],
  })
}

onMounted(() => {
  render()
  window.addEventListener('resize', () => chart?.resize())
})

watch(() => [props.ou, props.residuals], render, { deep: true })
</script>

<style scoped>
.ou-chart { width: 100%; min-height: 300px; }
</style>
