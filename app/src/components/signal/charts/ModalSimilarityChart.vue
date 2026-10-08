<template>
  <div class="modal-similarity-chart" ref="chartRef"></div>
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
  const dates = vmd_rolling.dates.slice(1) // 相关系数比窗口数少1
  const modalSimilarity = vmd_rolling.stability.modal_similarity
  const corrSeries = modalSimilarity.correlation_series
  const numImfs = corrSeries.length

  // 颜色方案
  const colors = ['#5470c6', '#91cc75', '#fac858', '#ee6666', '#73c0de', '#3ba272', '#fc8452', '#9a60b4']

  // 构建系列：每个 IMF 的相关系数轨迹
  const series: any[] = []
  for (let k = 0; k < numImfs; k++) {
    series.push({
      name: `IMF${k + 1}`,
      type: 'line',
      data: corrSeries[k],
      smooth: true,
      lineStyle: { width: 2 },
      symbol: 'none',
      itemStyle: { color: colors[k % colors.length] },
      markLine: {
        silent: true,
        symbol: 'none',
        lineStyle: { type: 'dashed', color: '#ff6b6b', width: 1 },
        data: [
          { yAxis: 0.7, label: { formatter: '稳定阈值 (0.7)', position: 'end' } }
        ]
      }
    })
  }

  // 计算平均相关系数和下降次数
  const avgCorr = modalSimilarity.avg_correlation.reduce((a, b) => a + b, 0) / modalSimilarity.avg_correlation.length
  const totalDrops = modalSimilarity.correlation_drops.reduce((a, b) => a + b, 0)

  const option = {
    backgroundColor: 'transparent',
    title: {
      text: '模态相似性分析（相邻窗口 IMF 相关系数）',
      subtext: `平均相关系数: ${avgCorr.toFixed(4)} | 突降次数: ${totalDrops} | 相似性评分: ${modalSimilarity.similarity_score.toFixed(1)}`,
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
          const value = typeof p.value === 'number' ? p.value.toFixed(4) : p.value
          const status = p.value < 0.7 ? '⚠️ 不稳定' : '✅ 稳定'
          result += `<div>${p.marker}${p.seriesName}: ${value} ${status}</div>`
        }
        return result
      }
    },
    legend: {
      data: corrSeries.map((_, i) => `IMF${i + 1}`),
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
      boundaryGap: false,
      axisLine: { lineStyle: { color: 'rgba(74, 85, 104, 0.3)' } },
      axisLabel: { color: '#999', rotate: 45 }
    },
    yAxis: {
      type: 'value',
      name: '相关系数 ρ',
      min: -1,
      max: 1,
      nameTextStyle: { color: '#999' },
      axisLine: { lineStyle: { color: 'rgba(74, 85, 104, 0.3)' } },
      axisLabel: { color: '#999', formatter: (v: number) => v.toFixed(2) },
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
.modal-similarity-chart {
  width: 100%;
  height: 100%;
}
</style>
