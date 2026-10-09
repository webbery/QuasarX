<template>
  <div ref="chartRef" class="state-probability-chart"></div>
</template>

<script setup lang="ts">
import { ref, onMounted, watch, onUnmounted } from 'vue'
import * as echarts from 'echarts'
import type { HMMResult } from '../composables/useHMMState'

const props = defineProps<{
  data: HMMResult
}>()

const chartRef = ref<HTMLElement | null>(null)
let chartInstance: echarts.ECharts | null = null

function renderChart() {
  if (!chartRef.value || !props.data) return

  if (!chartInstance) {
    chartInstance = echarts.init(chartRef.value)
  }

  const { probs, dates, states } = props.data
  const n_states = Math.max(...states) + 1
  
  // 为每个状态创建一条堆叠面积线
  const series: any[] = []
  const stateColors = ['#26a65b', '#ea3943', '#5b8ff9', '#f5a623', '#9b59b6', '#1abc9c']

  for (let s = 0; s < n_states; s++) {
    const data = dates.map((_, idx) => probs[idx]?.[s] || 0)
    
    series.push({
      name: `状态 ${s}`,
      type: 'line',
      stack: 'total',
      areaStyle: {
        color: stateColors[s % stateColors.length],
        opacity: 0.7
      },
      lineStyle: {
        width: 1,
        color: stateColors[s % stateColors.length]
      },
      itemStyle: {
        color: stateColors[s % stateColors.length]
      },
      symbol: 'none',
      data,
      emphasis: {
        focus: 'series'
      }
    })
  }

  const option = {
    title: {
      text: '状态概率分布',
      textStyle: { color: '#e0e0e0', fontSize: 14 },
      left: 'center'
    },
    tooltip: {
      trigger: 'axis',
      formatter: (params: any) => {
        let result = `${params[0].axisValue}<br/>`
        params.forEach((param: any) => {
          result += `${param.marker} ${param.seriesName}: ${(param.value * 100).toFixed(1)}%<br/>`
        })
        return result
      }
    },
    legend: {
      data: series.map(s => s.name),
      textStyle: { color: '#999' },
      top: 30
    },
    grid: {
      left: '3%',
      right: '4%',
      bottom: '3%',
      top: '20%',
      containLabel: true
    },
    xAxis: {
      type: 'category',
      data: dates,
      boundaryGap: false,
      axisLabel: {
        color: '#999',
        rotate: 30
      },
      axisLine: {
        lineStyle: { color: '#444' }
      }
    },
    yAxis: {
      type: 'value',
      max: 1,
      axisLabel: {
        color: '#999',
        formatter: (value: number) => `${(value * 100).toFixed(0)}%`
      },
      splitLine: {
        lineStyle: { color: '#333' }
      }
    },
    series
  }

  chartInstance.setOption(option, true)
}

onMounted(() => {
  renderChart()
})

watch(() => props.data, () => {
  renderChart()
}, { deep: true })

onUnmounted(() => {
  chartInstance?.dispose()
})
</script>

<style scoped>
.state-probability-chart {
  width: 100%;
  height: 100%;
}
</style>
