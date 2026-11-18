<template>
  <div class="ui-view">
    <div ref="chartRef" class="ui-view__canvas" :style="{ height: height + 'px' }"></div>

    <!-- 关键时刻条（紧凑态隐藏） -->
    <div v-if="moments.length && density !== 'compact'" class="ui-moments">
      <span class="moments-label">关键时刻</span>
      <button
        v-for="(m, i) in moments"
        :key="m.id"
        type="button"
        class="moment"
        :class="'is-' + (m.severity || 'info')"
        :data-tooltip="m.detail || m.title"
        @mouseenter="focusMoment(i)"
        @focus="focusMoment(i)"
        @click="focusMoment(i)"
      >
        <span class="moment-time">{{ formatMoment(m.timestamp) }}</span>
        <span class="moment-title">{{ m.title }}</span>
      </button>
    </div>
  </div>
</template>

<script setup lang="ts">
import { computed, nextTick, onMounted, watch } from 'vue'
import * as echarts from 'echarts'
import { useECharts } from '@/components/report/composables/useECharts'
import { buildTimeseriesOption, type TimeSeriesData } from '@/lib/charts/options/timeseries'
import type { ArtifactDensity } from '@/lib/agent/artifacts'
import type { UiMoment } from '@/lib/charts/types'

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
  const d = props.data as unknown as TimeSeriesData & { moments?: UiMoment[] }
  return {
    x: d?.x ?? [],
    cols: d?.cols ?? [],
    markers: d?.markers ?? [],
    x_label: d?.x_label,
    y_label: d?.y_label,
    y_format: d?.y_format,
  }
})

const moments = computed<UiMoment[]>(
  () => ((props.data as { moments?: UiMoment[] })?.moments ?? []),
)

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

/** 悬停时刻条目 → 在图上报出该点 */
function focusMoment(index: number) {
  const m = moments.value[index]
  const x = seriesData.value.x
  if (!m || !x.length || !chartRef.value) return

  let idx = -1
  let bestDiff = Infinity
  for (let i = 0; i < x.length; i++) {
    const diff = Math.abs(x[i] - m.timestamp)
    if (diff < bestDiff) {
      bestDiff = diff
      idx = i
    }
  }
  if (idx < 0) return
  echarts.getInstanceByDom(chartRef.value)?.dispatchAction({
    type: 'showTip',
    seriesIndex: 0,
    dataIndex: idx,
  })
}

function formatMoment(ts: number): string {
  const d = new Date(ts > 1e11 ? ts : ts * 1000)
  const y = d.getFullYear()
  const mm = String(d.getMonth() + 1).padStart(2, '0')
  const dd = String(d.getDate()).padStart(2, '0')
  return `${y}-${mm}-${dd}`
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

.ui-moments {
  display: flex;
  flex-wrap: wrap;
  align-items: center;
  gap: 6px;
  padding: 8px 13px 0;
  border-top: 1px solid rgba(74, 85, 104, 0.22);
  margin-top: 4px;
}

.moments-label {
  font-size: 11px;
  color: #6f7b8a;
  margin-right: 2px;
}

.moment {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  padding: 2px 9px;
  border-radius: 20px;
  font-size: 11px;
  cursor: pointer;
  font-family: inherit;
  background: rgba(255, 255, 255, 0.04);
  border: 1px solid rgba(74, 85, 104, 0.4);
  color: #a0aec0;
}

.moment:hover {
  border-color: #2962ff;
  color: #e0e0e0;
}

.moment .moment-time {
  font-variant-numeric: tabular-nums;
  color: #6f7b8a;
}

.moment.is-warning {
  border-color: rgba(255, 152, 0, 0.5);
  color: #ff9800;
}

.moment.is-critical {
  border-color: rgba(239, 83, 80, 0.55);
  color: #ef5350;
}

.moment.is-positive {
  border-color: rgba(0, 200, 83, 0.5);
  color: #00c853;
}
</style>
