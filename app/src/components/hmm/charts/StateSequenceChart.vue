<template>
  <div ref="chartRef" class="state-sequence-chart"></div>
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

  const { states, dates } = props.data
  
  // 为不同状态分配颜色
  const stateColors = ['#26a65b', '#ea3943', '#5b8ff9', '#f5a623', '#9b59b6', '#1abc9c']
  
  // 按状态分段绘制
  const series: any[] = []
  let current_state = states[0]
  let start_idx = 0

  for (let i = 1; i <= states.length; i++) {
    if (i === states.length || states[i] !== current_state) {
      // 绘制当前状态段
      const segment_data = dates.slice(start_idx, i).map((date, idx) => {
        const actual_idx = start_idx + idx
        return [date, actual_idx === start_idx || actual_idx === i - 1 ? current_state : null]
      })

      series.push({
        name: `状态 ${current_state}`,
        type: 'line',
        data: segment_data,
        lineStyle: {
          width: 3,
          color: stateColors[current_state % stateColors.length]
        },
        itemStyle: {
          color: stateColors[current_state % stateColors.length]
        },
        symbol: 'none',
        connectNulls: false
      })

      if (i < states.length) {
        current_state = states[i]
        start_idx = i
      }
    }
  }

  const option = {
    title: {
      text: '隐状态时序',
      textStyle: { color: '#e0e0e0', fontSize: 14 },
      left: 'center'
    },
    tooltip: {
      trigger: 'axis',
      formatter: (params: any) => {
        const param = params[0]
        return `${param.data[0]}<br/>状态: ${param.data[1]}`
      }
    },
    grid: {
      left: '3%',
      right: '4%',
      bottom: '3%',
      top: '15%',
      containLabel: true
    },
    xAxis: {
      type: 'category',
      data: dates,
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
      min: 0,
      max: Math.max(...states) + 1,
      axisLabel: {
        color: '#999',
        formatter: (value: number) => `状态 ${value}`
      },
      splitLine: {
        lineStyle: { color: '#333' }
      }
    },
    series: series.length > 0 ? series : [{
      type: 'line',
      data: dates.map((date, idx) => [date, states[idx]]),
      lineStyle: { width: 2 },
      symbol: 'none'
    }]
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
.state-sequence-chart {
  width: 100%;
  height: 100%;
}
</style>
