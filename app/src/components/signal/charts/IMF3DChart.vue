<template>
  <div class="imf-3d-chart" ref="chartRef" style="width: 100%; height: 100%"></div>
</template>

<script setup lang="ts">
import { watch, onMounted, onBeforeUnmount } from 'vue'
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
  if (!props.data) return {}

  const { dates, imf_components } = props.data
  const numIMFs = imf_components.length
  if (numIMFs === 0) return {}

  const xAxisData = dates && dates.length > 0
    ? dates.map((d: string) => d.length > 10 ? d.substring(5, 10) : d)
    : imf_components[0].map((_: number, i: number) => String(i))

  // 构建 3D 曲面数据：每个 IMF 作为一条沿 Y 轴分布的曲线
  // data[i] = [x(时间索引), y(IMF索引), z(振幅值)]
  const surfaceData: number[][] = []
  for (let imfIdx = 0; imfIdx < numIMFs; imfIdx++) {
    const imf = imf_components[imfIdx]
    for (let t = 0; t < imf.length; t++) {
      surfaceData.push([t, imfIdx, imf[t]])
    }
  }

  // Y 轴标签：IMF + 周期
  const yAxisLabels = imf_info.map((info, idx) => `IMF${idx + 1} T=${info.mean_period.toFixed(0)}`)
  const colorList = IMF_COLORS.slice(0, numIMFs)

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
      textStyle: { color: '#e0e0e0', fontSize: 11 },
      formatter: (params: any) => {
        const [tIdx, imfIdx, val] = params.data
        const dateStr = xAxisData[tIdx] || tIdx
        return `<b>IMF${imfIdx + 1}</b><br/>时间: ${dateStr}<br/>值: ${val.toFixed(6)}`
      }
    },
    visualMap: {
      show: false,
      min: 0,
      max: numIMFs - 1,
      inRange: {
        color: colorList
      },
      dimension: 1
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
        color: (idx: number) => IMF_COLORS[idx % IMF_COLORS.length],
        fontSize: 10
      },
      axisLine: { lineStyle: { color: 'rgba(74, 85, 104, 0.4)' } }
    },
    zAxis3D: {
      name: '振幅',
      axisLabel: {
        color: '#999',
        fontSize: 10,
        formatter: (val: number) => val.toFixed(4)
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
      },
      environment: '#1a2236',
      postEffect: {
        enable: false
      },
      light: {
        main: {
          intensity: 0.8,
          shadow: false
        },
        ambient: {
          intensity: 0.2
        }
      },
      axisLine: { lineStyle: { color: 'rgba(74, 85, 104, 0.4)' } },
      axisPointer: {
        show: true,
        lineStyle: { color: '#2962ff' },
        label: { show: true }
      }
    },
    series: [{
      type: 'line3D',
      name: 'IMF',
      data: surfaceData,
      lineStyle: {
        width: 2
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

watch(() => props.data, () => {
  if (props.data) {
    if (!chartInstance && chartRef.value) {
      initChart()
    } else {
      updateChart()
    }
  }
}, { immediate: true })

onMounted(() => {
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
.imf-3d-chart {
  min-height: 500px;
}
</style>
