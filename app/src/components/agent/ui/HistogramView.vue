<template>
  <div class="ui-view">
    <div ref="chartRef" class="ui-view__canvas" :style="{ height: height + 'px' }"></div>
  </div>
</template>

<script setup lang="ts">
import { computed, nextTick, onMounted, watch } from 'vue'
import * as echarts from 'echarts'
import { useECharts } from '@/components/report/composables/useECharts'
import { buildHistogramOption, type HistogramData } from '@/lib/charts/options/histogram'
import type { ArtifactDensity } from '@/lib/agent/artifacts'

const props = withDefaults(
  defineProps<{
    data: Record<string, unknown>
    params?: Record<string, unknown>
    height?: number
    density?: ArtifactDensity
  }>(),
  { height: 300, density: 'full' },
)

const { chartRef, initChart, updateChart } = useECharts(false)

const option = computed(() =>
  buildHistogramOption(props.data as unknown as HistogramData),
)

function render() {
  if (!chartRef.value) return
  if (!echarts.getInstanceByDom(chartRef.value)) {
    initChart()
  }
  updateChart(option.value, true)
}

onMounted(() => nextTick(render))

watch(
  () => props.data,
  () => nextTick(render),
  { deep: true },
)
</script>

<style scoped>
.ui-view {
  width: 100%;
}

.ui-view__canvas {
  width: 100%;
}
</style>
