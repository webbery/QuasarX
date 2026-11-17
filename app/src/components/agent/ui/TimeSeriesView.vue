<template>
  <div class="ui-view">
    <div ref="chartRef" class="ui-view__canvas" :style="{ height: height + 'px' }"></div>
  </div>
</template>

<script setup lang="ts">
import { computed, nextTick, onMounted, watch } from 'vue'
import * as echarts from 'echarts'
import { useECharts } from '@/components/report/composables/useECharts'
import { buildTimeseriesOption, type TimeSeriesData } from '@/lib/charts/options/timeseries'
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

/** 列式数据 → builder 的入参 */
const seriesData = computed<TimeSeriesData>(() => {
  const d = props.data as unknown as TimeSeriesData
  return {
    x: d?.x ?? [],
    cols: d?.cols ?? [],
    markers: d?.markers ?? [],
    x_label: d?.x_label,
    y_label: d?.y_label,
    y_format: d?.y_format,
  }
})

const option = computed(() => {
  const p = props.params ?? {}
  return buildTimeseriesOption(seriesData.value, {
    chartType: (p.chart_type as any) ?? 'line',
    smooth: p.smooth as boolean | undefined,
    secondaryAxisIndex: p.secondary_axis as number | undefined,
    secondaryAxisLabel: p.secondary_axis_label as string | undefined,
    legend: p.legend as boolean | undefined,
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
