<template>
  <div ref="chartRef" class="state-radar-chart"></div>
</template>

<script setup lang="ts">
import { ref, onMounted, watch, onUnmounted } from 'vue'
import * as echarts from 'echarts'
import type { HMMResult } from '../composables/useHMMState'

const props = defineProps<{
  data: HMMResult
  features: string[]
}>()

const chartRef = ref<HTMLElement | null>(null)
let chartInstance: echarts.ECharts | null = null

function renderChart() {
  if (!chartRef.value || !props.data) return

  if (!chartInstance) {
    chartInstance = echarts.init(chartRef.value)
  }

  const { state_means, states } = props.data
  const n_states = Math.max(...states) + 1
  const features = props.features
  
  const stateColors = ['#26a65b', '#ea3943', '#5b8ff9', '#f5a623', '#9b59b6', '#1abc9c']

  // 计算每个特征的最大值用于归一化
  const max_vals = features.map((_, feat_idx) => {
    return Math.max(...state_means.map(state => Math.abs(state[feat_idx] || 0)))
  })

  // 构建雷达图数据
  const series_data: any[] = []
  for (let s = 0; s < n_states; s++) {
    const values = features.map((_, feat_idx) => {
      const val = state_means[s]?.[feat_idx] || 0
      return max_vals[feat_idx] > 0 ? val / max_vals[feat_idx] : 0
    })

    series_data.push({
      name: `状态 ${s}`,
      value: values,
      lineStyle: {
        color: stateColors[s % stateColors.length]
      },
      itemStyle: {
        color: stateColors[s % stateColors.length]
      },
      areaStyle: {
        color: stateColors[s % stateColors.length],
        opacity: 0.2
      }
    })
  }

  const option = {
    title: {
      text: '状态特征均值分布',
      textStyle: { color: '#e0e0e0', fontSize: 14 },
      left: 'center'
    },
    tooltip: {
      formatter: (params: any) => {
        let result = `${params.name}<br/>`
        params.value.forEach((val: number, idx: number) => {
          const actual_val = state_means[params.seriesIndex]?.[idx] || 0
          result += `${features[idx]}: ${actual_val.toFixed(4)}<br/>`
        })
        return result
      }
    },
    legend: {
      data: series_data.map(s => s.name),
      textStyle: { color: '#999' },
      top: 30
    },
    radar: {
      indicator: features.map(f => ({
        name: f,
        max: 1
      })),
      shape: 'circle',
      splitNumber: 5,
      axisName: {
        color: '#999'
      },
      splitLine: {
        lineStyle: { color: '#333' }
      },
      splitArea: {
        areaStyle: {
          color: ['rgba(255,255,255,0.02)', 'rgba(255,255,255,0.05)']
        }
      },
      axisLine: {
        lineStyle: { color: '#444' }
      }
    },
    series: [{
      type: 'radar',
      data: series_data
    }]
  }

  chartInstance.setOption(option, true)
}

onMounted(() => {
  renderChart()
})

watch(() => [props.data, props.features], () => {
  renderChart()
}, { deep: true })

onUnmounted(() => {
  chartInstance?.dispose()
})
</script>

<style scoped>
.state-radar-chart {
  width: 100%;
  height: 100%;
}
</style>
