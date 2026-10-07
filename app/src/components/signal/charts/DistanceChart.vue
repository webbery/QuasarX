<template>
  <div class="distance-chart" ref="chartRef"></div>
</template>

<script setup lang="ts">
import { ref, onMounted, onUnmounted, watch } from 'vue'
import * as echarts from 'echarts'
import type { SignalAnalysisResult } from '../composables/useSignalState'

const props = defineProps<{
  data: SignalAnalysisResult | null
}>()

const chartRef = ref<HTMLElement | null>(null)
let chart: echarts.ECharts | null = null
let resizeObserver: ResizeObserver | null = null

function renderChart() {
  if (!chartRef.value || !props.data?.vmd_rolling) return
  
  // 检查容器尺寸（v-show 守卫）
  if (chartRef.value.offsetWidth === 0 || chartRef.value.offsetHeight === 0) {
    return
  }

  if (!chart) {
    chart = echarts.init(chartRef.value)
  }

  const { vmd_rolling } = props.data
  const dates = vmd_rolling.dates
  const freqTraj = vmd_rolling.center_freq_trajectory
  const numImfs = freqTraj.length

  // 计算相邻IMF之间的距离矩阵 [n_windows][K-1]
  const distanceMatrix: number[][] = []
  const imfPairs: string[] = []

  // 构建IMF对标签
  for (let k = 0; k < numImfs - 1; k++) {
    imfPairs.push(`IMF${k + 1}-IMF${k + 2}`)
  }

  // 计算每个窗口中相邻IMF的距离
  for (let t = 0; t < dates.length; t++) {
    const row: number[] = []
    for (let k = 0; k < numImfs - 1; k++) {
      const freq1 = freqTraj[k][t] || 0
      const freq2 = freqTraj[k + 1][t] || 0
      row.push(Math.abs(freq2 - freq1))
    }
    distanceMatrix.push(row)
  }

  // 转换为热力图数据格式 [x, y, value]
  const heatmapData: number[][] = []
  for (let t = 0; t < dates.length; t++) {
    for (let k = 0; k < imfPairs.length; k++) {
      heatmapData.push([t, k, distanceMatrix[t][k]])
    }
  }

  // 计算颜色范围
  const allValues = heatmapData.map(d => d[2])
  const minVal = Math.min(...allValues)
  const maxVal = Math.max(...allValues)

  const option = {
    backgroundColor: 'transparent',
    title: {
      text: '相邻 IMF 中心频率距离',
      subtext: `最小距离: ${minVal.toFixed(4)} | 平均距离: ${(allValues.reduce((a, b) => a + b, 0) / allValues.length).toFixed(4)} | 混叠阈值: 0.1`,
      left: 'center',
      top: 10,
      textStyle: { color: '#e0e0e0', fontSize: 16 },
      subtextStyle: { color: '#999', fontSize: 12 }
    },
    tooltip: {
      position: 'top',
      backgroundColor: 'rgba(26, 34, 54, 0.9)',
      borderColor: 'rgba(74, 85, 104, 0.3)',
      textStyle: { color: '#e0e0e0' },
      formatter: (params: any) => {
        const dateIdx = params.data[0]
        const pairIdx = params.data[1]
        const distance = params.data[2]
        const date = dates[dateIdx]
        const pair = imfPairs[pairIdx]
        const status = distance < 0.1 ? '⚠️ 混叠风险' : '✅ 正常'
        return `
          <div style="font-weight:bold">${date}</div>
          <div>${pair}: ${distance.toFixed(4)}</div>
          <div style="color: ${distance < 0.1 ? '#ee6666' : '#91cc75'}">${status}</div>
        `
      }
    },
    grid: {
      left: 100,
      right: 80,
      top: 80,
      bottom: 60
    },
    xAxis: {
      type: 'category',
      data: dates,
      splitArea: { show: true },
      axisLine: { lineStyle: { color: 'rgba(74, 85, 104, 0.3)' } },
      axisLabel: { color: '#999', rotate: 45 }
    },
    yAxis: {
      type: 'category',
      data: imfPairs,
      splitArea: { show: true },
      axisLine: { lineStyle: { color: 'rgba(74, 85, 104, 0.3)' } },
      axisLabel: { color: '#e0e0e0', fontSize: 11 }
    },
    visualMap: {
      min: minVal,
      max: maxVal,
      calculable: false,
      show: true,
      orient: 'vertical',
      right: 10,
      top: 'center',
      text: ['距离大', '距离小'],
      textStyle: { color: '#e0e0e0' },
      inRange: {
        color: ['#ee6666', '#fac858', '#91cc75']
      }
    },
    series: [
      {
        name: '频率距离',
        type: 'heatmap',
        data: heatmapData,
        label: {
          show: false
        },
        emphasis: {
          itemStyle: {
            shadowBlur: 10,
            shadowColor: 'rgba(0, 0, 0, 0.5)'
          }
        }
      }
    ]
  }

  chart.setOption(option)
}

function handleResize() {
  chart?.resize()
}

onMounted(() => {
  renderChart()
  window.addEventListener('resize', handleResize)
  
  // 添加 ResizeObserver 监听容器尺寸变化
  if (chartRef.value) {
    resizeObserver = new ResizeObserver(() => {
      handleResize()
      // 延迟重新渲染，确保容器已完全显示
      setTimeout(() => renderChart(), 100)
    })
    resizeObserver.observe(chartRef.value)
  }
})

onUnmounted(() => {
  window.removeEventListener('resize', handleResize)
  resizeObserver?.disconnect()
  chart?.dispose()
  chart = null
})

watch(() => props.data, () => {
  renderChart()
}, { deep: true })
</script>

<style scoped>
.distance-chart {
  width: 100%;
  height: 100%;
}
</style>
