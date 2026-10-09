<template>
  <div ref="chartRef" class="duration-chart"></div>
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

  const { duration, states } = props.data
  const n_states = Math.max(...states) + 1
  
  const stateColors = ['#26a65b', '#ea3943', '#5b8ff9', '#f5a623', '#9b59b6', '#1abc9c']

  const option = {
    title: {
      text: '状态期望持续时间',
      textStyle: { color: '#e0e0e0', fontSize: 14 },
      left: 'center'
    },
    tooltip: {
      formatter: (params: any) => {
        return `${params.name}<br/>期望持续: ${params.value.toFixed(1)} 天`
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
      data: Array.from({ length: n_states }, (_, i) => `状态 ${i}`),
      axisLabel: {
        color: '#999'
      },
      axisLine: {
        lineStyle: { color: '#444' }
      }
    },
    yAxis: {
      type: 'value',
      name: '天数',
      axisLabel: {
        color: '#999'
      },
      splitLine: {
        lineStyle: { color: '#333' }
      }
    },
    series: [{
      type: 'bar',
      data: duration.slice(0, n_states).map((val, idx) => ({
        value: val,
        itemStyle: {
          color: stateColors[idx % stateColors.length]
        }
      })),
      barWidth: '50%',
      label: {
        show: true,
        position: 'top',
        color: '#e0e0e0',
        formatter: (params: any) => `${params.value.toFixed(1)}天`
      }
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
.duration-chart {
  width: 100%;
  height: 100%;
}
</style>
