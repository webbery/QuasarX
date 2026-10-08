<template>
  <div class="energy-trajectory-chart" ref="chartRef"></div>
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
  const energyTraj = vmd_rolling.energy_trajectory
  const stability = vmd_rolling.stability
  const numImfs = energyTraj.length

  // 颜色方案
  const colors = ['#5470c6', '#91cc75', '#fac858', '#ee6666', '#73c0de', '#3ba272', '#fc8452', '#9a60b4']

  // 构建系列：每个 IMF 的能量占比轨迹
  const series: any[] = []
  for (let k = 0; k < numImfs; k++) {
    series.push({
      name: `IMF${k + 1}`,
      type: 'line',
      stack: 'energy',
      areaStyle: { opacity: 0.3 },
      data: energyTraj[k],
      smooth: true,
      lineStyle: { width: 1.5 },
      symbol: 'none',
      itemStyle: { color: colors[k % colors.length] },
      emphasis: { focus: 'series' }
    })
  }

  // 添加能量熵曲线（右侧Y轴）
  series.push({
    name: '能量熵',
    type: 'line',
    yAxisIndex: 1,
    data: stability.energy_entropy,
    smooth: true,
    lineStyle: { width: 3, type: 'dashed', color: '#ff6b6b' },
    symbol: 'circle',
    symbolSize: 6,
    itemStyle: { color: '#ff6b6b' }
  })

  // 计算平均能量波动率
  const avgVolatility = stability.energy_volatility.reduce((a, b) => a + b, 0) / stability.energy_volatility.length

  const option = {
    backgroundColor: 'transparent',
    title: {
      text: '模态能量占比轨迹 & 能量熵',
      subtext: `平均能量波动率: ${avgVolatility.toFixed(2)}% | 熵稳定性评分: ${stability.entropy_stability_score.toFixed(1)}`,
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
          result += `<div>${p.marker}${p.seriesName}: ${value}</div>`
        }
        return result
      },
      axisPointer: {
        type: 'cross',
        label: { backgroundColor: '#6a7985' }
      }
    },
    legend: {
      data: [...energyTraj.map((_, i) => `IMF${i + 1}`), '能量熵'],
      top: 60,
      textStyle: { color: '#e0e0e0' }
    },
    grid: {
      left: 60,
      right: 80,
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
    yAxis: [
      {
        type: 'value',
        name: '能量占比 (%)',
        nameTextStyle: { color: '#999' },
        axisLine: { lineStyle: { color: 'rgba(74, 85, 104, 0.3)' } },
        axisLabel: { color: '#999' },
        splitLine: { lineStyle: { color: 'rgba(74, 85, 104, 0.1)' } }
      },
      {
        type: 'value',
        name: '能量熵',
        nameTextStyle: { color: '#ff6b6b' },
        axisLine: { lineStyle: { color: '#ff6b6b' } },
        axisLabel: { color: '#ff6b6b', formatter: (v: number) => v.toFixed(2) },
        splitLine: { show: false }
      }
    ],
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
.energy-trajectory-chart {
  width: 100%;
  height: 100%;
}
</style>
