<template>
  <div class="pv-divergence" ref="chartRef" style="width: 100%; height: 100%"></div>
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

function buildOption() {
  const lf = props.result?.lowest_freq
  if (!lf || !lf.energy_series || !lf.energy_series.length) {
    return {}
  }
  const rolling = props.result?.rolling
  if (!rolling?.dates?.length) return {}

  const dates = rolling.dates
  const energy = lf.energy_series.map(v => +(v * 100).toFixed(2))
  const vol = lf.volume_normalized.map(v => +(v * 100).toFixed(2))
  const ratio = lf.energy_to_volume_ratio.map(v => +v.toFixed(3))

  return {
    title: {
      text: `量价分离诊断 (最低频 IMF${lf.imf_index + 1}, 周期=${lf.imf_mean_period.toFixed(0)})`,
      left: 'center',
      textStyle: { color: '#e0e0e0', fontSize: 13 }
    },
    tooltip: {
      trigger: 'axis',
      axisPointer: { type: 'cross' },
      backgroundColor: 'rgba(26, 34, 54, 0.95)',
      borderColor: '#2a3449',
      textStyle: { color: '#e0e0e0', fontSize: 11 },
      formatter: (params: any) => {
        if (!params || !params.length) return ''
        const date = params[0].axisValueLabel
        const lines = params.map((p: any) => {
          if (p.seriesName.includes('比值')) {
            return `${p.marker} ${p.seriesName}: ${p.value.toFixed(3)}`
          }
          return `${p.marker} ${p.seriesName}: ${p.value.toFixed(2)}%`
        })
        return `<b>${date}</b><br/>${lines.join('<br/>')}`
      }
    },
    legend: {
      data: ['最低频 IMF 能量', '标准化成交量', '能量/成交量比值'],
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
        name: '占比 %',
        nameTextStyle: { color: '#a0aec0', fontSize: 10 },
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
        name: '比值',
        nameTextStyle: { color: '#a0aec0', fontSize: 10 },
        axisLabel: {
          color: '#a0aec0',
          fontSize: 10
        },
        axisLine: { lineStyle: { color: 'rgba(74, 85, 104, 0.4)' } },
        splitLine: { show: false }
      }
    ],
    dataZoom: [
      { type: 'inside' },
      { type: 'slider', height: 18, bottom: 8, backgroundColor: 'rgba(26, 34, 54, 0.6)' }
    ],
    series: [
      {
        name: '最低频 IMF 能量',
        type: 'line',
        smooth: true,
        showSymbol: false,
        lineStyle: { color: '#2962ff', width: 2 },
        itemStyle: { color: '#2962ff' },
        areaStyle: { color: 'rgba(41, 98, 255, 0.15)' },
        data: energy
      },
      {
        name: '标准化成交量',
        type: 'line',
        smooth: true,
        showSymbol: false,
        lineStyle: { color: '#ff9800', width: 2, type: 'dashed' },
        itemStyle: { color: '#ff9800' },
        data: vol
      },
      {
        name: '能量/成交量比值',
        type: 'line',
        yAxisIndex: 1,
        smooth: true,
        showSymbol: false,
        lineStyle: { color: '#ff5252', width: 1.5 },
        itemStyle: { color: '#ff5252' },
        data: ratio,
        markLine: {
          silent: true,
          symbol: 'none',
          lineStyle: { color: '#a0aec0', type: 'dashed', width: 1 },
          data: [
            {
              yAxis: 1,
              label: { show: true, formatter: '同步基线 1', color: '#a0aec0', fontSize: 10 }
            }
          ]
        },
        z: 3
      }
    ]
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
.pv-divergence {
  width: 100%;
  height: 100%;
  min-height: 320px;
}
</style>
