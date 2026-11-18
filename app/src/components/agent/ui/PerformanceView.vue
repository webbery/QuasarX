<template>
  <div class="ui-view">
    <div ref="chartRef" class="ui-view__canvas" :style="{ height: height + 'px' }"></div>
  </div>
</template>

<script setup lang="ts">
import { computed, nextTick, onMounted, watch } from 'vue'
import * as echarts from 'echarts'
import { useECharts } from '@/components/report/composables/useECharts'
import { buildPerformanceOption } from '@/lib/charts/options/performance'
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

interface Column {
  n: string
  v: Array<number | string | null>
}

const x = computed<number[]>(() => (props.data?.x as number[]) ?? [])
const cols = computed<Column[]>(() => (props.data?.cols as Column[]) ?? [])

/** 百分比数据（0.0123）→ 百分数（1.23），与 report 侧口径一致 */
const isPercent = computed(() => props.data?.y_format === 'percent')

function toPercentValue(v: number | string | null): number | null {
  if (typeof v !== 'number' || !Number.isFinite(v)) return null
  return isPercent.value ? Number((v * 100).toFixed(2)) : v
}

function findCol(keyword: string): Column | undefined {
  return cols.value.find((c) => c.n.includes(keyword))
}

const strategyCol = computed(() => findCol('策略') ?? findCol('累计'))
const benchmarkCol = computed(() => findCol('基准'))
const drawdownCol = computed(() => findCol('回撤'))

/** 时间戳 → 'YYYY-MM-DD' */
function formatDate(ts: number): string {
  const d = new Date(ts > 1e11 ? ts : ts * 1000)
  const y = d.getFullYear()
  const m = String(d.getMonth() + 1).padStart(2, '0')
  const dd = String(d.getDate()).padStart(2, '0')
  return `${y}-${m}-${dd}`
}

const option = computed(() =>
  buildPerformanceOption({
    dates: x.value.map(formatDate),
    cumulativeReturns: (strategyCol.value?.v ?? []).map(toPercentValue),
    benchmarkCumulative: benchmarkCol.value?.v.map(toPercentValue),
    benchmarkName: benchmarkCol.value?.n.replace(/^基准收益/, '基准'),
    drawdown: drawdownCol.value?.v.map(toPercentValue),
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
