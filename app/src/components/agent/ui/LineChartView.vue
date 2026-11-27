<template>
  <div class="ui-view">
    <div ref="chartRef" class="ui-view__canvas" :style="{ height: height + 'px' }"></div>
    <slot></slot>
  </div>
</template>

<script setup lang="ts">
import { computed, nextTick, onMounted, watch } from 'vue'
import * as echarts from 'echarts'
import { useECharts } from '@/components/report/composables/useECharts'
import type { EChartsOption } from 'echarts'
import type { ArtifactDensity } from '@/lib/agent/artifacts'
import { buildTimeseriesOption } from '@/lib/charts/options/timeseries'

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

const option = computed<EChartsOption>(() => {
  const params = props.params ?? {}
  return buildTimeseriesOption(props.data as any, {
    chartType: (params.chart_type as any) ?? 'line',
    smooth: params.smooth as boolean | undefined,
    secondaryAxisIndex: params.secondary_axis as number | undefined,
    secondaryAxisLabel: params.secondary_axis_label as string | undefined,
    legend: params.legend as boolean | undefined,
  })
})

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
