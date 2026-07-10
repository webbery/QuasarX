<template>
  <div ref="chartEl" class="chart-container"></div>
</template>

<script setup lang="ts">
import { onMounted, onBeforeUnmount, watch, ref } from 'vue'
import * as echarts from 'echarts'
import type { ShapResult } from '../composables/useXGBoostState'

const props = defineProps<{ data: ShapResult }>()

let chart: echarts.ECharts | null = null
const chartEl = ref<HTMLDivElement | null>(null)

function render() {
  if (!chart) return
  // 计算每个特征的平均 |SHAP|
  const features = props.data.features
  const n = props.data.shap.length
  if (n === 0) {
    chart.setOption({ title: { text: 'SHAP 数据为空', left: 'center' } }, true)
    return
  }
  const means = features.map((_, idx) => {
    let sum = 0
    for (const row of props.data.shap) sum += Math.abs(row[idx] ?? 0)
    return sum / n
  })
  const order = means.map((_, i) => i).sort((a, b) => means[b] - means[a])
  const sortedFeats = order.map(i => features[i])
  const sortedMeans = order.map(i => Number(means[i].toFixed(4)))

  const opts = {
    title: { text: 'SHAP 摘要（平均 |SHAP|）', left: 'center' },
    tooltip: { trigger: 'axis', axisPointer: { type: 'shadow' } },
    grid: { left: 100, right: 30, top: 50, bottom: 30 },
    xAxis: { type: 'value', name: 'mean |SHAP|' },
    yAxis: {
      type: 'category',
      data: sortedFeats,
      inverse: true,
    },
    series: [{
      name: 'mean |SHAP|',
      type: 'bar',
      data: sortedMeans,
      itemStyle: { color: '#fac858' },
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
watch(() => props.data, render, { deep: true })
</script>

<style scoped>
.chart-container { width: 100%; height: 360px; }
</style>
