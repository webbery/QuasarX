<template>
  <div ref="chartRef" class="log-likelihood-chart"></div>
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

  const { log_likelihood } = props.data

  const option = {
    title: {
      text: '对数似然收敛曲线',
      textStyle: { color: '#e0e0e0', fontSize: 14 },
      left: 'center'
    },
    tooltip: {
      trigger: 'axis',
      formatter: (params: any) => {
        const param = params[0]
        return `迭代 ${param.dataIndex}<br/>对数似然: ${param.value.toFixed(2)}`
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
      name: '迭代次数',
      data: Array.from({ length: log_likelihood.length }, (_, i) => i),
      axisLabel: {
        color: '#999'
      },
      axisLine: {
        lineStyle: { color: '#444' }
      }
    },
    yAxis: {
      type: 'value',
      name: '对数似然',
      axisLabel: {
        color: '#999'
      },
      splitLine: {
        lineStyle: { color: '#333' }
      }
    },
    series: [{
      type: 'line',
      data: log_likelihood,
      smooth: true,
      symbol: 'circle',
      symbolSize: 6,
      lineStyle: {
        width: 2,
        color: '#5b8ff9'
      },
      itemStyle: {
        color: '#5b8ff9'
      },
      areaStyle: {
        color: new echarts.graphic.LinearGradient(0, 0, 0, 1, [
          { offset: 0, color: 'rgba(91, 143, 249, 0.3)' },
          { offset: 1, color: 'rgba(91, 143, 249, 0.05)' }
        ])
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
.log-likelihood-chart {
  width: 100%;
  height: 100%;
}
</style>
