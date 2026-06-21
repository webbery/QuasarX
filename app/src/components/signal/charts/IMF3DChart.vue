<template>
  <div class="imf-3d-chart" ref="chartRef" style="width: 100%; height: 100%; min-height: 500px; background: rgba(26, 34, 54, 0.3);">
  </div>
</template>

<script setup lang="ts">
import { ref, watch, onMounted, onBeforeUnmount } from 'vue'
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
  if (!props.data) {
    console.warn('[IMF3DChart] No data')
    return {}
  }

  const { dates, imf_components, imf_info } = props.data
  const numIMFs = imf_components.length
  
  console.log('[IMF3DChart] buildOption called with:', {
    dates_count: dates?.length || 0,
    imf_count: numIMFs,
    imf_info_count: imf_info?.length || 0,
    method: props.data.method,
    reconstruction_error: props.data.reconstruction_error
  })
  
  if (numIMFs === 0 || !imf_info || imf_info.length === 0) {
    console.warn('[IMF3DChart] No imf_components or imf_info')
    return {}
  }

  const xAxisData = dates && dates.length > 0
    ? dates.map((d: string) => d.length > 10 ? d.substring(5, 10) : d)
    : imf_components[0].map((_: number, i: number) => String(i))

  // 每个 IMF 作为独立的 series，避免线连在一起
  const series = imf_components.map((imf, imfIdx) => {
    const lineData = imf.map((val, t) => [t, imfIdx, val])
    return {
      type: 'line3D',
      name: `IMF${imfIdx + 1}`,
      data: lineData,
      lineStyle: {
        width: 2,
        color: IMF_COLORS[imfIdx % IMF_COLORS.length]
      }
    }
  })

  // Y 轴标签：IMF + 周期
  const yAxisLabels = imf_info.map((info, idx) => `IMF${idx + 1} T=${info.mean_period.toFixed(0)}`)
  const colorList = IMF_COLORS.slice(0, numIMFs)
  console.log('[IMF3DChart] yAxisLabels:', yAxisLabels)
  console.log('[IMF3DChart] colorList:', colorList)
  console.log('[IMF3DChart] series count:', series.length)
  console.log('[IMF3DChart] About to return option object')

  return {
    title: {
      text: `IMF 分量 3D 视图 (${props.data.method.toUpperCase()})  重建误差: ${(props.data.reconstruction_error * 1e10).toFixed(2)}e-10`,
      left: 'center',
      textStyle: { color: '#e0e0e0', fontSize: 13 }
    },
    tooltip: {
      trigger: 'item',
      backgroundColor: 'rgba(26, 34, 54, 0.95)',
      borderColor: '#2a3449',
      textStyle: { color: '#e0e0e0', fontSize: 11 }
    },
    xAxis3D: {
      name: '时间',
      type: 'category',
      data: xAxisData,
      axisLabel: {
        color: '#999',
        fontSize: 10,
        interval: Math.floor(xAxisData.length / 8)
      },
      axisLine: { lineStyle: { color: 'rgba(74, 85, 104, 0.4)' } }
    },
    yAxis3D: {
      name: 'IMF',
      type: 'category',
      data: yAxisLabels,
      axisLabel: {
        color: '#999',
        fontSize: 10
      },
      axisLine: { lineStyle: { color: 'rgba(74, 85, 104, 0.4)' } }
    },
    zAxis3D: {
      name: '振幅',
      axisLabel: {
        color: '#999',
        fontSize: 10
      },
      axisLine: { lineStyle: { color: 'rgba(74, 85, 104, 0.4)' } },
      splitLine: { lineStyle: { color: 'rgba(74, 85, 104, 0.15)' } }
    },
    grid3D: {
      boxWidth: 200,
      boxDepth: 120,
      boxHeight: 60,
      viewControl: {
        autoRotate: false,
        alpha: 20,
        beta: 40,
        distance: 200,
        minAlpha: 5,
        maxAlpha: 90,
        minBeta: -180,
        maxBeta: 180,
        minDistance: 80,
        maxDistance: 400
      }
    },
    series
  }
}

function initChart() {
  console.log('[IMF3DChart] initChart called')
  console.log('[IMF3DChart] chartRef:', chartRef.value)
  console.log('[IMF3DChart] chartRef dimensions:', chartRef.value ? {
    offsetWidth: chartRef.value.offsetWidth,
    offsetHeight: chartRef.value.offsetHeight,
    clientWidth: chartRef.value.clientWidth,
    clientHeight: chartRef.value.clientHeight
  } : 'N/A')
  
  if (!chartRef.value) {
    console.warn('[IMF3DChart] chartRef not ready')
    return
  }

  try {
    const existingInstance = echarts.getInstanceByDom(chartRef.value)
    console.log('[IMF3DChart] existingInstance:', existingInstance ? 'yes' : 'no')
    
    if (existingInstance) {
      console.log('[IMF3DChart] disposing existing instance')
      echarts.dispose(existingInstance)
    }
    
    console.log('[IMF3DChart] creating new echarts instance')
    chartInstance = echarts.init(chartRef.value)
    console.log('[IMF3DChart] chartInstance created:', chartInstance)
    
    const option = buildOption()
    console.log('[IMF3DChart] option keys:', Object.keys(option))
    console.log('[IMF3DChart] option has series:', option.series ? 'yes' : 'no')
    console.log('[IMF3DChart] series type:', option.series?.[0]?.type)
    console.log('[IMF3DChart] series data length:', option.series?.[0]?.data?.length || 0)
    
    if (Object.keys(option).length > 0) {
      console.log('[IMF3DChart] calling setOption')
      chartInstance.setOption(option)
      console.log('[IMF3DChart] Chart initialized successfully')
    } else {
      console.warn('[IMF3DChart] option is empty, skipping setOption')
    }
  } catch (e) {
    console.error('[IMF3DChart] initChart error:', e)
    console.error('[IMF3DChart] error stack:', (e as Error).stack)
  }
}

function updateChart() {
  console.log('[IMF3DChart] updateChart called')
  
  if (!chartInstance) {
    console.warn('[IMF3DChart] chartInstance not ready')
    return
  }

  try {
    const option = buildOption()
    console.log('[IMF3DChart] updateChart option keys:', Object.keys(option))
    console.log('[IMF3DChart] updateChart series data length:', option.series?.[0]?.data?.length || 0)
    
    if (Object.keys(option).length > 0) {
      console.log('[IMF3DChart] calling setOption with notMerge=true')
      chartInstance.setOption(option, true)
      console.log('[IMF3DChart] Chart updated successfully')
    }
  } catch (e) {
    console.error('[IMF3DChart] updateChart error:', e)
    console.error('[IMF3DChart] error stack:', (e as Error).stack)
  }
}

watch(() => props.data, (newData) => {
  console.log('[IMF3DChart] watch triggered, newData:', newData ? 'yes' : 'no')
  console.log('[IMF3DChart] chartInstance:', chartInstance ? 'yes' : 'no')
  console.log('[IMF3DChart] chartRef.value:', chartRef.value ? 'yes' : 'no')
  
  if (newData) {
    if (!chartInstance && chartRef.value) {
      console.log('[IMF3DChart] Initializing chart from watch')
      initChart()
    } else if (chartInstance) {
      console.log('[IMF3DChart] Updating chart from watch')
      updateChart()
    } else {
      console.warn('[IMF3DChart] Cannot init or update: chartInstance=', chartInstance, 'chartRef=', chartRef.value)
    }
  }
}, { immediate: true })

onMounted(() => {
  console.log('[IMF3DChart] onMounted')
  console.log('[IMF3DChart] props.data:', props.data ? 'yes' : 'no')
  console.log('[IMF3DChart] chartRef.value:', chartRef.value ? 'yes' : 'no')
  console.log('[IMF3DChart] chartRef dimensions:', chartRef.value ? {
    offsetWidth: chartRef.value.offsetWidth,
    offsetHeight: chartRef.value.offsetHeight
  } : 'N/A')

  // 在 onMounted 中初始化图表
  if (props.data && chartRef.value) {
    console.log('[IMF3DChart] onMounted: calling initChart')
    initChart()
  }

  const handleResize = () => {
    console.log('[IMF3DChart] resize event')
    chartInstance?.resize()
  }
  window.addEventListener('resize', handleResize)

  onBeforeUnmount(() => {
    console.log('[IMF3DChart] onBeforeUnmount')
    window.removeEventListener('resize', handleResize)
    if (chartInstance) {
      console.log('[IMF3DChart] disposing chartInstance')
      chartInstance.dispose()
      chartInstance = null
    }
  })
})

</script>

<style scoped>
.imf-3d-chart {
  min-height: 500px;
}
</style>
