<template>
  <div ref="chartRef" class="transition-matrix-chart"></div>
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

  const { transition, states } = props.data
  const n_states = Math.max(...states) + 1
  
  // 准备热力图数据
  const heat_data: [number, number, number][] = []
  for (let i = 0; i < n_states; i++) {
    for (let j = 0; j < n_states; j++) {
      heat_data.push([j, i, transition[i][j]])
    }
  }

  const option = {
    title: {
      text: '状态转移矩阵',
      textStyle: { color: '#e0e0e0', fontSize: 14 },
      left: 'center'
    },
    tooltip: {
      formatter: (params: any) => {
        const [from, to, value] = params.data
        return `从状态 ${from} 到状态 ${to}<br/>概率: ${(value * 100).toFixed(1)}%`
      }
    },
    grid: {
      left: '10%',
      right: '10%',
      bottom: '15%',
      top: '15%'
    },
    xAxis: {
      type: 'category',
      data: Array.from({ length: n_states }, (_, i) => `状态 ${i}`),
      axisLabel: {
        color: '#999'
      },
      axisLine: {
        lineStyle: { color: '#444' }
      },
      splitArea: {
        show: true,
        areaStyle: {
          color: ['rgba(255,255,255,0.02)', 'rgba(255,255,255,0.05)']
        }
      }
    },
    yAxis: {
      type: 'category',
      data: Array.from({ length: n_states }, (_, i) => `状态 ${i}`),
      axisLabel: {
        color: '#999'
      },
      axisLine: {
        lineStyle: { color: '#444' }
      },
      splitArea: {
        show: true,
        areaStyle: {
          color: ['rgba(255,255,255,0.02)', 'rgba(255,255,255,0.05)']
        }
      }
    },
    visualMap: {
      min: 0,
      max: 1,
      calculable: true,
      orient: 'horizontal',
      left: 'center',
      bottom: '0%',
      inRange: {
        color: ['#1a2236', '#2962ff', '#5b8ff9', '#82c1ff']
      },
      textStyle: {
        color: '#999'
      },
      formatter: (value: number) => `${(value * 100).toFixed(0)}%`
    },
    series: [{
      name: '转移概率',
      type: 'heatmap',
      data: heat_data,
      label: {
        show: true,
        color: '#e0e0e0',
        formatter: (params: any) => {
          return (params.data[2] * 100).toFixed(1) + '%'
        }
      },
      emphasis: {
        itemStyle: {
          shadowBlur: 10,
          shadowColor: 'rgba(0, 0, 0, 0.5)'
        }
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
.transition-matrix-chart {
  width: 100%;
  height: 100%;
}
</style>
