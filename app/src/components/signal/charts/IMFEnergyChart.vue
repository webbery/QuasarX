<template>
  <div class="imf-energy" ref="chartRef" style="width: 100%; height: 100%"></div>
</template>

<script setup lang="ts">
import { watch, onMounted, onBeforeUnmount, ref } from 'vue'
import * as echarts from 'echarts'
import type { SignalAnalysisResult } from '../composables/useSignalState'

const props = defineProps<{
  data: SignalAnalysisResult | null
}>()

const chartRef = ref<HTMLDivElement>()
let chartInstance: echarts.EChartsType | null = null

const IMF_COLORS = [
  '#2962ff', '#ff9800', '#00c853', '#ff6d00', '#a0aec0',
  '#e040fb', '#ffab40', '#69f0ae', '#ff5252', '#7c4dff',
  '#40c4ff', '#ffd740', '#b2ff59', '#ff6e40', '#ea80fc'
]

function buildOption() {
  if (!props.data || !props.data.imf_info || !props.data.imf_info.length) {
    console.warn('[IMFEnergyChart] No data or empty imf_info')
    return {}
  }

  const { imf_info, residual } = props.data

  // 各 IMF 能量占比
  const energyVals = imf_info.map(info => info.energy_pct || 0)
  // 残差能量占比
  const imfSumOfSquares = imf_info.reduce((sum, info) => sum + (info.energy_pct || 0), 0)
  const totalVariance = residual?.reduce((sum, v) => sum + v * v, 0) || 0
  const imfTotalVariance = imf_info.reduce((sum, info, idx) => {
    const imf = props.data!.imf_components?.[idx] || []
    return sum + imf.reduce((s, v) => s + v * v, 0)
  }, 0)
  const grandTotal = imfTotalVariance + totalVariance
  const residualPct = grandTotal > 0 ? (totalVariance / grandTotal) * 100 : 0
  const labels = imf_info.map((info, idx) => `IMF${idx + 1}\nT=${info.mean_period.toFixed(0)}`)

  // 累计能量
  let acc = 0
  const cumulative = energyVals.map(v => { acc += v; return +acc.toFixed(2) })

  return {
    title: {
      text: 'IMF 能量分布',
      left: 'center',
      textStyle: { color: '#e0e0e0', fontSize: 13 }
    },
    tooltip: {
      trigger: 'axis',
      axisPointer: { type: 'shadow' },
      backgroundColor: 'rgba(26, 34, 54, 0.95)',
      borderColor: '#2a3449',
      textStyle: { color: '#e0e0e0', fontSize: 11 },
      formatter: (params: any) => {
        const imfIdx = params[0].dataIndex
        const info = imf_info[imfIdx]
        const cum = cumulative[imfIdx]
        const rows = params.map((p: any) => {
          const marker = p.marker ?? ''
          return `${marker} ${p.seriesName}: ${p.value.toFixed(2)}%`
        }).join('<br/>')
        return `<b>IMF${imfIdx + 1}</b> · 周期 ${info.mean_period.toFixed(1)}<br/>${rows}<br/><span style="color:#a0aec0">累计解释: ${cum.toFixed(1)}%</span>`
      }
    },
    legend: {
      data: ['IMF 能量', '累计能量', '残差能量'],
      top: 24,
      textStyle: { color: '#a0aec0', fontSize: 11 }
    },
    grid: {
      left: 50,
      right: 50,
      top: 60,
      bottom: 50
    },
    xAxis: {
      type: 'category',
      data: labels,
      axisLabel: {
        color: '#a0aec0',
        fontSize: 10,
        interval: 0
      },
      axisLine: { lineStyle: { color: 'rgba(74, 85, 104, 0.4)' } },
      axisTick: { show: false }
    },
    yAxis: [
      {
        type: 'value',
        name: '能量 %',
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
        name: '累计 %',
        nameTextStyle: { color: '#a0aec0', fontSize: 10 },
        min: 0,
        max: 100,
        axisLabel: {
          color: '#a0aec0',
          fontSize: 10,
          formatter: '{value}%'
        },
        axisLine: { lineStyle: { color: 'rgba(74, 85, 104, 0.4)' } },
        splitLine: { show: false }
      }
    ],
    series: [
      {
        name: 'IMF 能量',
        type: 'bar',
        data: energyVals.map((v, idx) => ({
          value: +v.toFixed(2),
          itemStyle: { color: IMF_COLORS[idx % IMF_COLORS.length] }
        })),
        barWidth: '55%',
        label: {
          show: true,
          position: 'top',
          formatter: (p: any) => `${p.value.toFixed(1)}%`,
          fontSize: 10,
          color: '#e0e0e0'
        }
      },
      {
        name: '累计能量',
        type: 'line',
        yAxisIndex: 1,
        data: cumulative,
        smooth: true,
        symbol: 'circle',
        symbolSize: 7,
        lineStyle: { color: '#ffd740', width: 2 },
        itemStyle: { color: '#ffd740', borderColor: '#1a2236', borderWidth: 2 },
        z: 3
      },
      {
        name: '残差能量',
        type: 'bar',
        data: imf_info.map(() => 0),
        stack: 'total',
        barWidth: '55%',
        itemStyle: { color: 'transparent' },
        tooltip: { show: false }
      },
      {
        name: '残差能量',
        type: 'bar',
        data: imf_info.map(() => +residualPct.toFixed(2)),
        stack: 'total',
        barWidth: '55%',
        itemStyle: {
          color: 'rgba(160, 174, 192, 0.45)',
          borderColor: '#a0aec0',
          borderWidth: 1,
          borderType: 'dashed'
        },
        label: {
          show: true,
          position: 'insideTop',
          formatter: () => `残差 ${residualPct.toFixed(1)}%`,
          fontSize: 10,
          color: '#e0e0e0'
        }
      }
    ]
  }
}

function initChart() {
  if (!chartRef.value) return
  if (echarts.getInstanceByDom(chartRef.value)) {
    echarts.dispose(echarts.getInstanceByDom(chartRef.value)!)
  }
  chartInstance = echarts.init(chartRef.value)
  const option = buildOption()
  if (Object.keys(option).length > 0) {
    chartInstance.setOption(option)
  }
}

function updateChart() {
  if (!chartInstance) return
  const option = buildOption()
  if (Object.keys(option).length > 0) {
    chartInstance.setOption(option, true)
  }
}

watch(() => props.data, (newData) => {
  if (newData) {
    setTimeout(() => {
      if (!chartInstance && chartRef.value) {
        initChart()
      } else if (chartInstance) {
        updateChart()
      }
    }, 0)
  }
}, { immediate: true })

onMounted(() => {
  if (props.data && chartRef.value) {
    setTimeout(() => initChart(), 0)
  }

  const handleResize = () => chartInstance?.resize()
  window.addEventListener('resize', handleResize)
  onBeforeUnmount(() => {
    window.removeEventListener('resize', handleResize)
    chartInstance?.dispose()
    chartInstance = null
  })
})
</script>

<style scoped>
.imf-energy {
  width: 100%;
  height: 100%;
  min-height: 320px;
}
</style>
