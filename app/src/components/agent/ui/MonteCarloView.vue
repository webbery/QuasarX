<template>
  <div class="ui-view">
    <div ref="chartRef" class="ui-view__canvas" :style="{ height: height + 'px' }"></div>
  </div>
</template>

<script setup lang="ts">
import { computed, nextTick, onMounted, watch } from 'vue'
import * as echarts from 'echarts'
import { useECharts } from '@/components/report/composables/useECharts'
import {
  buildMonteCarloOption,
  type MonteCarloMode,
} from '@/lib/charts/options/monteCarlo'
import type { McPathDetail } from '@/stores/history'
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

const mode = computed<MonteCarloMode>(
  () => ((props.params?.mode as MonteCarloMode) ?? 'worst'),
)

const option = computed(() =>
  buildMonteCarloOption({
    mode: mode.value,
    worstPaths: (props.data?.worst as McPathDetail[]) ?? [],
    bestPaths: (props.data?.best as McPathDetail[]) ?? [],
    medianPath: (props.data?.median as McPathDetail) ?? null,
    p10Path: (props.data?.p10 as McPathDetail) ?? null,
    p90Path: (props.data?.p90 as McPathDetail) ?? null,
    barCount: props.data?.bar_count as number | undefined,
  }),
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
  () => [props.data, props.params],
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
