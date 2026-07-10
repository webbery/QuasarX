<template>
  <div ref="chartEl" class="chart-container"></div>
</template>

<script setup lang="ts">
import { onMounted, onBeforeUnmount, watch, ref } from 'vue'
import * as echarts from 'echarts'
import type { FeatureImportance } from '../composables/useXGBoostState'

type Metric = 'gain' | 'weight' | 'cover'
const props = defineProps<{ data: FeatureImportance[]; metric?: Metric }>()
const currentMetric = ref<Metric>(props.metric || 'gain')

let chart: echarts.ECharts | null = null
const chartEl = ref<HTMLDivElement | null>(null)

function render() {
  if (!chart) return
  const sorted = [...props.data].sort((a, b) => b[currentMetric.value] - a[currentMetric.value]).slice(0, 20)
  const opts = {
    title: { text: `特征重要性 (${currentMetric.value})`, left: 'center' },
    tooltip: { trigger: 'axis', axisPointer: { type: 'shadow' } },
    grid: { left: 100, right: 30, top: 50, bottom: 30 },
    xAxis: { type: 'value', name: currentMetric.value },
    yAxis: {
      type: 'category',
      data: sorted.map(d => d.feature),
      inverse: true,
    },
    series: [{
      name: currentMetric.value,
      type: 'bar',
      data: sorted.map(d => d[currentMetric.value]),
      itemStyle: { color: '#73c0de' },
    }],
  }
  chart.setOption(opts, true)
}

onMounted(() => {
  if (chartEl.value) {
    chart = echarts.init(chartEl.value)
    render()
  }
  window.addEventListener('resize', handleResize)
})

onBeforeUnmount(() => {
  window.removeEventListener('resize', handleResize)
  chart?.dispose()
  chart = null
})

function handleResize() { chart?.resize() }

watch(() => [props.data, currentMetric.value], render, { deep: true })

function setMetric(m: Metric) { currentMetric.value = m }
defineExpose({ setMetric })
</script>

<style scoped>
.chart-container { width: 100%; height: 360px; }
</style>
