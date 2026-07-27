<template>
  <div ref="chartEl" class="chart-container"></div>
</template>

<script setup lang="ts">
import { computed, onMounted, onBeforeUnmount, watch, ref } from 'vue'
import * as echarts from 'echarts'
import type { FeatureImportance } from '../composables/useXGBoostState'

type Metric = 'gain' | 'weight' | 'cover'
const props = defineProps<{ data: FeatureImportance[]; metric?: Metric }>()
const currentMetric = ref<Metric>(props.metric || 'gain')
const propsMetric = computed(() => props.metric)
const emit = defineEmits<{ (e: 'metricChange', value: Metric): void }>()

let chart: echarts.ECharts | null = null
const chartEl = ref<HTMLDivElement | null>(null)

const metricLabel = computed(() => {
  switch (currentMetric.value) {
    case 'gain': return 'Gain'
    case 'weight': return 'Weight'
    case 'cover': return 'Cover'
    default: return currentMetric.value
  }
})

watch(propsMetric, (next) => {
  if (next && next !== currentMetric.value) {
    currentMetric.value = next
  }
})

function render() {
  if (!chart) return
  const sorted = [...props.data].sort((a, b) => b[currentMetric.value] - a[currentMetric.value]).slice(0, 20)
  const opts = {
    backgroundColor: 'transparent',
    title: {
      text: `特征重要性 (${metricLabel.value})`,
      left: 'center',
      textStyle: { color: '#e2e8f0', fontSize: 13 },
    },
    tooltip: { trigger: 'axis', axisPointer: { type: 'shadow' }, backgroundColor: 'rgba(15,25,41,0.95)', borderColor: '#2b3a55', textStyle: { color: '#e0e0e0' } },
    grid: { left: 110, right: 30, top: 50, bottom: 30 },
    xAxis: { type: 'value', name: metricLabel.value, nameTextStyle: { color: '#94a3b8' }, axisLabel: { color: '#94a3b8' }, splitLine: { lineStyle: { color: 'rgba(255,255,255,0.05)' } } },
    yAxis: {
      type: 'category',
      data: sorted.map(d => d.feature),
      inverse: true,
      axisLabel: { color: '#cbd5e1' },
    },
    series: [{
      name: metricLabel.value,
      type: 'bar',
      data: sorted.map(d => d[currentMetric.value]),
      itemStyle: { color: currentMetric.value === 'gain' ? '#5b8ff9' : currentMetric.value === 'weight' ? '#26a65b' : '#fac858' },
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
