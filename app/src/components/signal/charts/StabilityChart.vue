<template>
  <div class="stability-chart" ref="chartRef"></div>
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

  const { vmd_rolling, imf_info } = props.data
  const dates = vmd_rolling.dates
  const freqTraj = vmd_rolling.center_freq_trajectory
  const stability = vmd_rolling.stability

  // 颜色方案
  const colors = ['#5470c6', '#91cc75', '#fac858', '#ee6666', '#73c0de', '#3ba272', '#fc8452', '#9a60b4']

  // 构建系列：每个 IMF 的中心频率轨迹
  const series: any[] = []
  for (let k = 0; k < freqTraj.length; k++) {
    series.push({
      name: `IMF${k + 1}`,
      type: 'line',
      data: freqTraj[k],
      smooth: true,
      lineStyle: { width: 2 },
      symbol: 'none',
      itemStyle: { color: colors[k % colors.length] }
    })
  }

  // 添加稳定性评分标注
  const avgSmoothness = stability.freq_smoothness.reduce((a, b) => a + b, 0) / stability.freq_smoothness.length
  const avgJumpRatio = stability.freq_jump_ratio.reduce((a, b) => a + b, 0) / stability.freq_jump_ratio.length

  const option = {
    backgroundColor: 'transparent',
    title: {
      text: 'VMD 中心频率轨迹',
      subtext: `稳定性评分: ${stability.overall_score.toFixed(1)} | 平均平滑度: ${avgSmoothness.toFixed(4)} | 跳变率: ${avgJumpRatio.toFixed(1)}%`,
      left: 'center',
      top: 10,
      textStyle: { color: '#e0e0e0', fontSize: 16 },
      subtextStyle: { color: '#999', fontSize: 12 }
    },
    tooltip: {
      trigger: 'axis',
      backgroundColor: 'rgba(26, 34, 54, 0.9)',
      borderColor: 'rgba(74, 85, 104, 0.3)',
      textStyle: { color: '#e0e0e0' },
      formatter: (params: any) => {
        let result = `<div style="font-weight:bold">${params[0].axisValue}</div>`
        for (const p of params) {
          const imfIdx = parseInt(p.seriesName.replace('IMF', '')) - 1
          const period = imf_info?.[imfIdx]?.mean_period || 0
          const value = typeof p.value === 'number' ? p.value.toFixed(4) : p.value
          const periodStr = period.toFixed(4)
          result += `<div>${p.marker}${p.seriesName}: ${value} (周期≈${periodStr})</div>`
        }
        return result
      }
    },
    legend: {
      data: freqTraj.map((_, i) => `IMF${i + 1}`),
      top: 60,
      textStyle: { color: '#e0e0e0' }
    },
    grid: {
      left: 60,
      right: 40,
      top: 100,
      bottom: 60
    },
    xAxis: {
      type: 'category',
      data: dates,
      axisLine: { lineStyle: { color: 'rgba(74, 85, 104, 0.3)' } },
      axisLabel: { color: '#999' }
    },
    yAxis: {
      type: 'value',
      name: '中心频率 (归一化)',
      nameTextStyle: { color: '#999' },
      axisLine: { lineStyle: { color: 'rgba(74, 85, 104, 0.3)' } },
      axisLabel: { color: '#999', formatter: (v: number) => v.toFixed(3) },
      splitLine: { lineStyle: { color: 'rgba(74, 85, 104, 0.1)' } }
    },
    series
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
.stability-chart {
  width: 100%;
  height: 100%;
}
</style>
