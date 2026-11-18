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
  buildPriceTrendOption,
  type PriceTrendPoint,
} from '@/lib/charts/options/priceTrend'
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

/** 时间戳 → 'YYYY-MM-DD' */
function formatDate(ts: number): string {
  const d = new Date(ts > 1e11 ? ts : ts * 1000)
  const y = d.getFullYear()
  const m = String(d.getMonth() + 1).padStart(2, '0')
  const dd = String(d.getDate()).padStart(2, '0')
  return `${y}-${m}-${dd}`
}

const x = computed<number[]>(() => (props.data?.x as number[]) ?? [])

const cols = computed<Column[]>(() => (props.data?.cols as Column[]) ?? [])

/** 找到名字匹配的列 */
function findCol(keyword: string): Column | undefined {
  return cols.value.find((c) => c.n.includes(keyword))
}

const priceCol = computed(() => findCol('价格'))
const buyCol = computed(() => findCol('买入'))
const sellCol = computed(() => findCol('卖出'))

/** 对齐数组 → 信号点 */
function toPoints(col: Column | undefined): PriceTrendPoint[] {
  if (!col) return []
  const points: PriceTrendPoint[] = []
  col.v.forEach((v, index) => {
    if (typeof v === 'number' && Number.isFinite(v)) {
      points.push({ index, price: v })
    }
  })
  return points
}

const option = computed(() =>
  buildPriceTrendOption({
    categories: x.value.map(formatDate),
    prices: (priceCol.value?.v ?? []).map((v) => (typeof v === 'number' ? v : 0)),
    buyPoints: toPoints(buyCol.value),
    sellPoints: toPoints(sellCol.value),
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
