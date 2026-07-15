<template>
  <div class="rolling-energy" ref="chartRef" style="width: 100%; height: 100%"></div>
</template>

<script setup lang="ts">
import { watch, onMounted, onBeforeUnmount, ref } from 'vue'
import * as echarts from 'echarts'
import type { SignalAnalysisResult } from '../composables/useSignalState'

const props = defineProps<{
  result: SignalAnalysisResult | null
}>()

const chartRef = ref<HTMLDivElement>()
let chartInstance: echarts.EChartsType | null = null

const IMF_COLORS = [
  '#2962ff', '#ff9800', '#00c853', '#ff6d00', '#a0aec0',
  '#e040fb', '#ffab40', '#69f0ae', '#ff5252', '#7c4dff'
]

function buildOption() {
  const rolling = props.result?.rolling
  if (!rolling || !rolling.dates || !rolling.dates.length) {
    return {}
  }
  const { dates, by_imf_energy, residual_energy, change_rate, window } = rolling

  // 堆叠图：每个 IMF 能量占比 + 残差
  const stackSeries = by_imf_energy.map((vals, idx) => ({
    name: `IMF${idx + 1}`,
    type: 'line',
    stack: 'energy',
    smooth: true,
    showSymbol: false,
    lineStyle: { width: 1, color: IMF_COLORS[idx % IMF_COLORS.length] },
    areaStyle: { opacity: 0.6 },
    emphasis: { focus: 'series' },
    data: vals.map(v => +(v * 100).toFixed(2))
  }))

  // 残差
  if (residual_energy && residual_energy.length) {
    stackSeries.push({
      name: '残差',
      type: 'line',
      stack: 'energy',
      smooth: true,
      showSymbol: false,
      lineStyle: { width: 1, color: '#a0aec0', type: 'dashed' },
      areaStyle: { opacity: 0.4 },
      data: residual_energy.map(v => +(v * 100).toFixed(2))
    } as any)
  }

  // 总能量变化率（右轴）
  const changeRateSeries = {
    name: '总能量变化率',
    type: 'line',
    yAxisIndex: 1,
    smooth: true,
    showSymbol: false,
    lineStyle: { color: '#ffd740', width: 2 },
    itemStyle: { color: '#ffd740' },
    data: change_rate.map(v => +v.toFixed(2)),
    markLine: {
      silent: true,
      symbol: 'none',
      lineStyle: { color: '#a0aec0', type: 'dashed', width: 1 },
      data: [{ yAxis: 0, label: { show: false } }]
    },
    z: 3
  }

  return {
    title: {
      text: `滚动能量结构 (窗口 ${window})`,
      left: 'center',
      textStyle: { color: '#e0e0e0', fontSize: 13 }
    },
    tooltip: {
      trigger: 'axis',
      axisPointer: { type: 'cross' },
      backgroundColor: 'rgba(26, 34, 54, 0.95)',
      borderColor: '#2a3449',
      textStyle: { color: '#e0e0e0', fontSize: 11 }
    },
    legend: {
      data: [...by_imf_energy.map((_, i) => `IMF${i + 1}`), '残差', '总能量变化率'],
      top: 24,
      textStyle: { color: '#a0aec0', fontSize: 11 }
    },
    grid: { left: 50, right: 60, top: 60, bottom: 50 },
    xAxis: {
      type: 'category',
      data: dates,
      axisLabel: { color: '#a0aec0', fontSize: 10 },
      axisLine: { lineStyle: { color: 'rgba(74, 85, 104, 0.4)' } }
    },
    yAxis: [
      {
        type: 'value',
        name: '能量 %',
        nameTextStyle: { color: '#a0aec0', fontSize: 10 },
        min: 0,
        max: 100,
        axisLabel: {
          color: '#a0aec0',
          fontSize: 10,
          formatter: '{value}%'
        },
        axisLine: { lineStyle: { color: 'rgba(74, 85, 104, 0.4)' } },
        splitLine: { lineStyle: { color: 'rgba(74, 85, 104, 0.15)' } }
      },
      {
        type: 'value',
        name: '变化率 %',
        nameTextStyle: { color: '#a0aec0', fontSize: 10 },
        axisLabel: {
          color: '#a0aec0',
          fontSize: 10,
          formatter: '{value}%'
        },
        axisLine: { lineStyle: { color: 'rgba(74, 85, 104, 0.4)' } },
        splitLine: { show: false }
      }
    ],
    dataZoom: [
      { type: 'inside' },
      { type: 'slider', height: 18, bottom: 8, backgroundColor: 'rgba(26, 34, 54, 0.6)' }
    ],
    series: [...stackSeries, changeRateSeries]
  }
}

function initChart() {
  if (!chartRef.value) return
  const existing = echarts.getInstanceByDom(chartRef.value)
  if (existing) existing.dispose()
  chartInstance = echarts.init(chartRef.value)
  const opt = buildOption()
  if (Object.keys(opt).length > 0) chartInstance.setOption(opt)
}

function updateChart() {
  if (!chartInstance) return
  const opt = buildOption()
  if (Object.keys(opt).length > 0) chartInstance.setOption(opt, true)
}

watch(() => props.result, (val) => {
  if (!val) return
  setTimeout(() => {
    if (!chartInstance) initChart()
    else updateChart()
  }, 0)
}, { immediate: true })

onMounted(() => {
  if (props.result) setTimeout(() => initChart(), 0)
  const handler = () => chartInstance?.resize()
  window.addEventListener('resize', handler)
  onBeforeUnmount(() => {
    window.removeEventListener('resize', handler)
    chartInstance?.dispose()
    chartInstance = null
  })
})
</script>

<style scoped>
.rolling-energy {
  width: 100%;
  height: 100%;
  min-height: 320px;
}
</style>
