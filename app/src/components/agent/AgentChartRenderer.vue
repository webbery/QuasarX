<template>
  <div class="agent-chart">
    <!-- 图例（仅多序列时显示） -->
    <div v-if="showLegend" class="agent-chart__legend">
      <button
        v-for="item in legendItems"
        :key="item.index"
        type="button"
        :aria-pressed="hidden[item.index] ? 'false' : 'true'"
        @click="toggleSeries(item.index)"
      >
        <span class="sw" :style="{ background: item.color }"></span>
        <span>{{ item.name }}</span>
      </button>
    </div>

    <!-- 图区 -->
    <div ref="chartRef" class="agent-chart__canvas" :style="{ height: chartHeight + 'px' }"></div>

    <!-- 时刻条 -->
    <div v-if="moments.length" class="agent-chart__moments">
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
import { computed, nextTick, onMounted, reactive, ref, watch } from 'vue'
import * as echarts from 'echarts'
import { useECharts, createBaseChartOption } from '@/components/report/composables/useECharts'
import type { ChartPayload, ChartMoment } from '@/lib/agent/artifacts'

const props = withDefaults(
  defineProps<{
    payload: ChartPayload
    height?: number
  }>(),
  { height: 300 },
)

/** 主题调色板（与 report 模块一致） */
const PALETTE = ['#2962ff', '#ff9800', '#00c853', '#ff6d00', '#a0aec0', '#9a67d1']
const POSITIVE = '#00c853'
const NEGATIVE = '#ef5350'

const { chartRef, initChart, updateChart } = useECharts(false)

/** 隐藏的序列下标 */
const hidden = reactive<Record<number, boolean>>({})

const moments = computed<ChartMoment[]>(() => props.payload.moments ?? [])
const showLegend = computed(() => props.payload.legend?.show !== false && legendItems.value.length > 1)

const chartHeight = computed(() => {
  const base = props.height ?? 300
  return moments.value.length ? base : base
})

const legendItems = computed(() => {
  const ts = props.payload.timeSeries
  if (!ts) return []
  return ts.labels.map((name, index) => ({
    index,
    name,
    color: PALETTE[index % PALETTE.length],
  }))
})

/* ---------- 工具函数 ---------- */

/** 统一成毫秒时间戳 */
function toMs(ts: number): number {
  return ts > 1e11 ? ts : ts * 1000
}

function formatDate(ts: number): string {
  const d = new Date(toMs(ts))
  const mm = String(d.getMonth() + 1).padStart(2, '0')
  const dd = String(d.getDate()).padStart(2, '0')
  return `${mm}-${dd}`
}

function formatMoment(ts: number): string {
  const d = new Date(toMs(ts))
  const y = d.getFullYear()
  const mm = String(d.getMonth() + 1).padStart(2, '0')
  const dd = String(d.getDate()).padStart(2, '0')
  return `${y}-${mm}-${dd}`
}

/** 百分比类数据 ×100 */
function scale(values: number[]): number[] {
  if (props.payload.yFormat !== 'percent') return values
  return values.map((v) => (Number.isFinite(v) ? v * 100 : 0))
}

function findNearestIndex(timestamps: number[], ts: number): number {
  if (!timestamps.length) return -1
  let best = 0
  let bestDiff = Infinity
  for (let i = 0; i < timestamps.length; i++) {
    const diff = Math.abs(timestamps[i] - ts)
    if (diff < bestDiff) {
      bestDiff = diff
      best = i
    }
  }
  return best
}

/* ---------- 构建 option ---------- */

function buildOption(): any {
  const p = props.payload

  // 热力图
  if (p.chartType === 'heatmap' && p.matrix) {
    return buildHeatmapOption(p)
  }

  // 直方图
  if (p.chartType === 'histogram' && p.histogram) {
    return buildHistogramOption(p)
  }

  const ts = p.timeSeries
  if (!ts) {
    return createBaseChartOption({ title: { text: '暂无数据', left: 'center', top: 'middle', textStyle: { color: '#6f7b8a', fontSize: 13 } } })
  }

  const categories = ts.timestamps.map(formatDate)
  const hasSecondary = !!p.secondaryAxis

  const series: any[] = ts.labels.map((name, i) => {
    const values = scale(ts.values[i] || [])
    const color = PALETTE[i % PALETTE.length]
    const isSecondary = hasSecondary && p.secondaryAxis!.seriesIndex === i

    const base: any = {
      name,
      type: p.chartType === 'area' ? 'line' : p.chartType === 'scatter' ? 'scatter' : p.chartType,
      data: values,
      showSymbol: false,
      yAxisIndex: isSecondary ? 1 : 0,
      lineStyle: { color, width: 2 },
      itemStyle: { color },
      emphasis: { focus: 'series' },
    }

    if (p.chartType === 'bar') {
      base.barMaxWidth = 24
    }
    if (p.chartType === 'area') {
      base.areaStyle = { color, opacity: 0.18 }
      base.smooth = false
    }

    // 标记：挂到第一条可见序列上
    if (i === 0) {
      const markers = buildMarkers(ts.timestamps)
      if (markers.markLine) base.markLine = markers.markLine
      if (markers.markPoint) base.markPoint = markers.markPoint
    }

    return base
  })

  const yAxis: any[] = [
    {
      type: 'value',
      name: p.yAxisLabel,
      nameTextStyle: { color: '#6f7b8a', fontSize: 11 },
      axisLabel: {
        color: '#a0aec0',
        fontSize: 11,
        formatter: (v: number) => (p.yFormat === 'percent' ? `${v.toFixed(0)}%` : `${v}`),
      },
      splitLine: { lineStyle: { color: '#2a3449', type: 'dashed' } },
      scale: p.yFormat !== 'percent',
    },
  ]

  if (hasSecondary) {
    yAxis.push({
      type: 'value',
      name: p.secondaryAxis?.yAxisLabel,
      nameTextStyle: { color: '#6f7b8a', fontSize: 11 },
      axisLabel: { color: '#a0aec0', fontSize: 11 },
      splitLine: { show: false },
    })
  }

  return createBaseChartOption({
    animation: false,
    tooltip: {
      trigger: 'axis',
      axisPointer: { type: 'cross' },
      backgroundColor: 'rgba(26, 34, 54, 0.94)',
      borderColor: '#2a3449',
      textStyle: { color: '#e0e0e0', fontSize: 11 },
      valueFormatter: (v: any) =>
        typeof v === 'number' ? (p.yFormat === 'percent' ? `${v.toFixed(2)}%` : v.toFixed(2)) : v,
    },
    legend: { show: false },
    grid: {
      left: 52,
      right: hasSecondary ? 56 : 20,
      top: 18,
      bottom: 34,
      containLabel: false,
    },
    xAxis: {
      type: 'category',
      data: categories,
      boundaryGap: p.chartType === 'bar',
      axisLabel: {
        color: '#6f7b8a',
        fontSize: 10,
        hideOverlap: true,
        formatter: (_: string, idx: number) => categories[idx] ?? '',
      },
      axisLine: { lineStyle: { color: '#2a3449' } },
      axisTick: { show: false },
    },
    yAxis,
    series: series.filter((_, i) => !hidden[i]),
  })
}

/** 组装 markLine / markPoint */
function buildMarkers(timestamps: number[]) {
  const markLine: any[] = []
  const markPoint: any[] = []

  for (const m of props.payload.markers ?? []) {
    if (m.type === 'vline' && m.timestamp !== undefined) {
      const idx = findNearestIndex(timestamps, m.timestamp)
      if (idx < 0) continue
      markLine.push({
        xAxis: idx,
        label: m.label
          ? {
              formatter: m.label,
              color: '#a0aec0',
              fontSize: 10,
              position: 'insideEndTop',
            }
          : { show: false },
        lineStyle: { color: m.color || '#ff9800', type: m.style || 'dashed', width: 1 },
      })
    } else if (m.type === 'hline' && m.value !== undefined) {
      markLine.push({
        yAxis: m.value,
        label: m.label ? { formatter: m.label, color: '#a0aec0', fontSize: 10 } : { show: false },
        lineStyle: { color: m.color || '#ff9800', type: m.style || 'dashed', width: 1 },
      })
    } else if (m.type === 'point' && m.timestamp !== undefined && m.value !== undefined) {
      const idx = findNearestIndex(timestamps, m.timestamp)
      if (idx < 0) continue
      markPoint.push({
        coord: [idx, m.value],
        name: m.label,
        symbolSize: 8,
        itemStyle: { color: m.color || '#ff9800' },
        label: { show: false },
      })
    }
  }

  return {
    markLine: markLine.length ? { silent: true, symbol: 'none', data: markLine } : null,
    markPoint: markPoint.length ? { silent: true, data: markPoint } : null,
  }
}

/** 直方图 */
function buildHistogramOption(p: ChartPayload): any {
  const h = p.histogram!
  // bins 通常是边界数组（长度 = counts.length + 1）
  const labels = h.counts.map((_, i) => {
    const left = h.bins[i]
    const right = h.bins[i + 1] ?? h.bins[i]
    if (left === undefined) return String(i)
    return `${(left * 100).toFixed(1)}~${(right * 100).toFixed(1)}%`
  })

  const colors = h.counts.map((_, i) => {
    const mid = (h.bins[i] + (h.bins[i + 1] ?? h.bins[i])) / 2
    return mid < 0 ? NEGATIVE : POSITIVE
  })

  return createBaseChartOption({
    animation: false,
    tooltip: {
      trigger: 'axis',
      backgroundColor: 'rgba(26, 34, 54, 0.94)',
      borderColor: '#2a3449',
      textStyle: { color: '#e0e0e0', fontSize: 11 },
    },
    grid: { left: 48, right: 20, top: 18, bottom: 46, containLabel: false },
    xAxis: {
      type: 'category',
      data: labels,
      axisLabel: { color: '#6f7b8a', fontSize: 10, rotate: 30, hideOverlap: true },
      axisLine: { lineStyle: { color: '#2a3449' } },
      axisTick: { show: false },
    },
    yAxis: {
      type: 'value',
      name: p.yAxisLabel,
      nameTextStyle: { color: '#6f7b8a', fontSize: 11 },
      axisLabel: { color: '#a0aec0', fontSize: 11 },
      splitLine: { lineStyle: { color: '#2a3449', type: 'dashed' } },
    },
    series: [
      {
        type: 'bar',
        data: h.counts.map((v, i) => ({ value: v, itemStyle: { color: colors[i], opacity: 0.75 } })),
        barMaxWidth: 26,
      },
    ],
  })
}

/** 热力图 */
function buildHeatmapOption(p: ChartPayload): any {
  const m = p.matrix!
  const data: number[][] = []
  let min = Infinity
  let max = -Infinity
  m.values.forEach((row, r) => {
    row.forEach((v, c) => {
      data.push([c, r, v])
      if (Number.isFinite(v)) {
        min = Math.min(min, v)
        max = Math.max(max, v)
      }
    })
  })
  if (!Number.isFinite(min)) {
    min = 0
    max = 1
  }

  return createBaseChartOption({
    animation: false,
    tooltip: {
      backgroundColor: 'rgba(26, 34, 54, 0.94)',
      borderColor: '#2a3449',
      textStyle: { color: '#e0e0e0', fontSize: 11 },
    },
    grid: { left: 64, right: 60, top: 16, bottom: 48, containLabel: false },
    xAxis: {
      type: 'category',
      data: m.cols,
      splitArea: { show: true },
      axisLabel: { color: '#6f7b8a', fontSize: 10 },
      axisLine: { lineStyle: { color: '#2a3449' } },
    },
    yAxis: {
      type: 'category',
      data: m.rows,
      splitArea: { show: true },
      axisLabel: { color: '#a0aec0', fontSize: 10 },
      axisLine: { lineStyle: { color: '#2a3449' } },
    },
    visualMap: {
      min,
      max,
      calculable: false,
      orient: 'vertical',
      right: 4,
      top: 'middle',
      itemWidth: 10,
      itemHeight: 90,
      textStyle: { color: '#6f7b8a', fontSize: 10 },
      inRange: { color: ['#ef5350', '#1a2236', '#00c853'] },
    },
    series: [
      {
        type: 'heatmap',
        data,
        label: { show: false },
        emphasis: { itemStyle: { borderColor: '#e0e0e0', borderWidth: 1 } },
      },
    ],
  })
}

/* ---------- 交互 ---------- */

function toggleSeries(index: number) {
  hidden[index] = !hidden[index]
  render()
}

/** 悬停时刻条 → 在图上报出该点 */
function focusMoment(momentIndex: number) {
  const m = moments.value[momentIndex]
  const ts = props.payload.timeSeries
  if (!m || !ts || !chartRef.value) return
  const idx = findNearestIndex(ts.timestamps, m.timestamp)
  if (idx < 0) return
  const chart = echarts.getInstanceByDom(chartRef.value)
  chart?.dispatchAction({ type: 'showTip', seriesIndex: 0, dataIndex: idx })
}

/* ---------- 渲染 ---------- */

function render() {
  if (!chartRef.value) return
  if (!echarts.getInstanceByDom(chartRef.value)) {
    initChart()
  }
  updateChart(buildOption(), true)
}

onMounted(() => {
  nextTick(render)
})

watch(
  () => props.payload,
  () => nextTick(render),
  { deep: true },
)
</script>

<style scoped>
.agent-chart {
  width: 100%;
}

.agent-chart__legend {
  display: flex;
  flex-wrap: wrap;
  gap: 14px;
  padding: 0 4px 6px;
}

.agent-chart__legend button {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  font-size: 11.5px;
  color: var(--qx-text-2, #a0aec0);
  background: transparent;
  border: none;
  cursor: pointer;
  padding: 2px 0;
  font-family: inherit;
}

.agent-chart__legend button[aria-pressed='false'] {
  opacity: 0.42;
}

.agent-chart__legend .sw {
  width: 11px;
  height: 2.5px;
  border-radius: 2px;
}

.agent-chart__canvas {
  width: 100%;
}

.agent-chart__moments {
  display: flex;
  flex-wrap: wrap;
  align-items: center;
  gap: 6px;
  padding: 8px 2px 0;
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
