<template>
  <div class="imf-energy-3d" ref="chartRef" style="width: 100%; height: 100%"></div>
</template>

<script setup lang="ts">
import { watch, onMounted, onBeforeUnmount, ref } from 'vue'
import * as echarts from 'echarts'
import 'echarts-gl'
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

  console.log('[IMFEnergyChart] Building option with:', {
    imf_info_count: imf_info.length,
    residual_length: residual?.length,
    sample_imf_info: imf_info[0]
  })

  // 计算残差能量占比
  const totalVariance = residual?.reduce((sum, v) => sum + v * v, 0) || 0
  console.log('[IMFEnergyChart] totalVariance:', totalVariance)

  imf_info.forEach(info => {
    // 已有 energy_pct
  })

  // 3D 柱状图数据: [IMF索引, 0(单柱), 能量占比]
  const barData = imf_info.map((info, idx) => [idx, 0, info.energy_pct || 0])
  console.log('[IMFEnergyChart] barData sample:', barData.slice(0, 3))

  const labels = imf_info.map((info, idx) => `IMF${idx + 1} T=${info.mean_period.toFixed(0)}`)

  return {
    title: {
      text: `IMF 能量占比 3D`,
      left: 'center',
      textStyle: { color: '#e0e0e0', fontSize: 13 }
    },
    tooltip: {
      trigger: 'item',
      backgroundColor: 'rgba(26, 34, 54, 0.95)',
      borderColor: '#2a3449',
      textStyle: { color: '#e0e0e0', fontSize: 11 },
      formatter: (params: any) => {
        const [imfIdx, _, pct] = params.data
        const info = imf_info[imfIdx]
        return `<b>IMF${imfIdx + 1}</b><br/>周期: ${info?.mean_period.toFixed(1)}<br/>能量占比: ${pct.toFixed(1)}%`
      }
    },
    visualMap: {
      show: false,
      min: 0,
      max: imf_info.length - 1,
      inRange: {
        color: IMF_COLORS.slice(0, imf_info.length)
      },
      dimension: 0
    },
    xAxis3D: {
      name: 'IMF',
      type: 'category',
      data: labels,
      axisLabel: {
        color: '#999',
        fontSize: 10
      },
      axisLine: { lineStyle: { color: 'rgba(74, 85, 104, 0.4)' } }
    },
    yAxis3D: {
      name: '',
      type: 'value',
      max: 1,
      show: false
    },
    zAxis3D: {
      name: '能量%',
      axisLabel: {
        color: '#999',
        fontSize: 10,
        formatter: (val: number) => val.toFixed(0) + '%'
      },
      axisLine: { lineStyle: { color: 'rgba(74, 85, 104, 0.4)' } },
      splitLine: { lineStyle: { color: 'rgba(74, 85, 104, 0.15)' } }
    },
    grid3D: {
      boxWidth: 160,
      boxDepth: 30,
      boxHeight: 60,
      viewControl: {
        autoRotate: false,
        alpha: 15,
        beta: 30,
        distance: 180,
        minAlpha: 5,
        maxAlpha: 90,
        minBeta: -180,
        maxBeta: 180,
        minDistance: 80,
        maxDistance: 400
      },
      // 完全移除 environment 和 postEffect
      light: {
        main: { intensity: 0.8, shadow: false },
        ambient: { intensity: 0.3 }
      },
      axisLine: { lineStyle: { color: 'rgba(74, 85, 104, 0.4)' } },
      axisPointer: {
        show: true,
        lineStyle: { color: '#2962ff' },
        label: { show: true }
      }
    },
    series: [{
      type: 'bar3D',
      name: 'Energy',
      data: barData,
      shading: 'lambert',
      bevelSize: 0.3,
      bevelSmoothness: 2,
      itemStyle: {
        opacity: 0.85
      },
      label: {
        show: true,
        distance: 2,
        formatter: (params: any) => `${params.data[2].toFixed(1)}%`,
        fontSize: 10,
        color: '#e0e0e0'
      }
    }]
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
.imf-energy-3d {
  width: 100%;
  height: 100%;
  min-height: 350px;
}
</style>
