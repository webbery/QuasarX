<!-- app/src/components/review/tabs/BasicMetricsTab.vue -->
<!-- 基础指标 Tab - 包含指标表格、收益分布、回撤分析 -->

<template>
  <div class="basic-metrics-tab">
    <!-- 指标表格（复用已有组件） -->
    <MetricsTable :metrics="metrics" />

    <!-- 收益分布直方图 -->
    <div class="chart-card">
      <div class="chart-title">
        <div class="title-icon">📊</div>
        <span>日收益分布</span>
      </div>
      <div class="chart-container" ref="returnDistRef"></div>
    </div>

    <!-- 回撤曲线 -->
    <div class="chart-card">
      <div class="chart-title">
        <div class="title-icon">📉</div>
        <span>回撤分析</span>
      </div>
      <div class="chart-container" ref="drawdownRef"></div>
    </div>

    <!-- 净值曲线 vs 基准 -->
    <div class="chart-card full-width">
      <div class="chart-title">
        <div class="title-icon">📈</div>
        <span>净值曲线对比</span>
      </div>
      <div class="chart-container" ref="navCompareRef"></div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, watch, onMounted, onUnmounted } from 'vue'
import * as echarts from 'echarts'
import MetricsTable from '@/components/report/MetricsTable.vue'
import { useECharts } from '@/components/report/composables/useECharts'

interface Props {
  metrics: Record<string, number>
  dailyReturns: [string, number][]
  navCurve: [string, number][]
  benchmarkCurve: [string, number][]
  drawdownCurve: [string, number][]
}

const props = defineProps<Props>()

// === 收益分布图表 ===
const returnDistRef = ref<HTMLElement | null>(null)
let returnDistChart: echarts.EChartsType | null = null

function initReturnDistChart() {
  if (!returnDistRef.value) return
  returnDistChart = echarts.init(returnDistRef.value, 'quasarx-dark')
}

function updateReturnDistChart() {
  if (!returnDistChart || props.dailyReturns.length === 0) return

  const returns = props.dailyReturns.map(([, ret]) => ret)

  // 计算直方图
  const bins = 50
  const min = Math.min(...returns)
  const max = Math.max(...returns)
  const binWidth = (max - min) / bins
  const histogram = new Array(bins).fill(0)

  returns.forEach(ret => {
    const binIndex = Math.min(Math.floor((ret - min) / binWidth), bins - 1)
    histogram[binIndex]++
  })

  const binCenters = histogram.map((_, i) => min + binWidth * (i + 0.5))

  const option = {
    tooltip: {
      trigger: 'axis',
      backgroundColor: 'rgba(26, 34, 54, 0.9)',
      borderColor: '#2a3449',
      textStyle: { color: '#e0e0e0' },
    },
    grid: { left: '3%', right: '4%', bottom: '12%', containLabel: true },
    xAxis: {
      type: 'category',
      data: binCenters.map(v => (v * 100).toFixed(2) + '%'),
      axisLine: { lineStyle: { color: '#6E7079' } },
      axisLabel: { color: '#a0aec0', rotate: 45 },
      splitLine: { show: false },
    },
    yAxis: {
      type: 'value',
      axisLine: { lineStyle: { color: '#6E7079' } },
      axisLabel: { color: '#a0aec0' },
      splitLine: { lineStyle: { color: '#2a3449', type: 'dashed' } },
    },
    series: [{
      name: '频次',
      type: 'bar',
      data: histogram,
      itemStyle: {
        color: new echarts.graphic.LinearGradient(0, 0, 0, 1, [
          { offset: 0, color: '#2962ff' },
          { offset: 1, color: '#1e4fd9' },
        ]),
      },
    }],
  }

  returnDistChart.setOption(option, true)
}

// === 回撤图表 ===
const drawdownRef = ref<HTMLElement | null>(null)
let drawdownChart: echarts.EChartsType | null = null

function initDrawdownChart() {
  if (!drawdownRef.value) return
  drawdownChart = echarts.init(drawdownRef.value, 'quasarx-dark')
}

function updateDrawdownChart() {
  if (!drawdownChart || props.drawdownCurve.length === 0) return

  const option = {
    tooltip: {
      trigger: 'axis',
      backgroundColor: 'rgba(26, 34, 54, 0.9)',
      borderColor: '#2a3449',
      textStyle: { color: '#e0e0e0' },
      formatter: (params: any) => {
        const val = params[0].value
        return `${params[0].axisValue}<br/>回撤: <span style="color: #ff1744; font-weight: bold;">${(val * 100).toFixed(2)}%</span>`
      },
    },
    grid: { left: '3%', right: '4%', bottom: '12%', containLabel: true },
    xAxis: {
      type: 'category',
      data: props.drawdownCurve.map(([date]) => date),
      axisLine: { lineStyle: { color: '#6E7079' } },
      axisLabel: { color: '#a0aec0', rotate: 45 },
      splitLine: { show: false },
    },
    yAxis: {
      type: 'value',
      axisLine: { lineStyle: { color: '#6E7079' } },
      axisLabel: { color: '#a0aec0', formatter: (v: number) => (v * 100).toFixed(1) + '%' },
      splitLine: { lineStyle: { color: '#2a3449', type: 'dashed' } },
    },
    series: [{
      name: '回撤',
      type: 'line',
      data: props.drawdownCurve.map(([, dd]) => dd),
      lineStyle: { width: 2 },
      itemStyle: { color: '#ff1744' },
      areaStyle: {
        color: new echarts.graphic.LinearGradient(0, 0, 0, 1, [
          { offset: 0, color: 'rgba(255, 23, 68, 0.3)' },
          { offset: 1, color: 'rgba(255, 23, 68, 0.05)' },
        ]),
      },
      smooth: true,
      showSymbol: false,
    }],
  }

  drawdownChart.setOption(option, true)
}

// === 净值对比图表 ===
const navCompareRef = ref<HTMLElement | null>(null)
let navCompareChart: echarts.EChartsType | null = null

function initNavCompareChart() {
  if (!navCompareRef.value) return
  navCompareChart = echarts.init(navCompareRef.value, 'quasarx-dark')
}

function updateNavCompareChart() {
  if (!navCompareChart || props.navCurve.length === 0) return

  // 对齐日期
  const dateMap = new Map<string, [number, number]>()
  props.navCurve.forEach(([date, nav]) => {
    if (!dateMap.has(date)) dateMap.set(date, [nav, 1.0])
  })
  props.benchmarkCurve.forEach(([date, bench]) => {
    if (dateMap.has(date)) {
      dateMap.get(date)![1] = bench
    }
  })

  const dates = Array.from(dateMap.keys())
  const navs = dateMap.values().map(([nav]) => nav)
  const benchs = dateMap.values().map(([, bench]) => bench)

  const option = {
    tooltip: {
      trigger: 'axis',
      backgroundColor: 'rgba(26, 34, 54, 0.9)',
      borderColor: '#2a3449',
      textStyle: { color: '#e0e0e0' },
    },
    legend: {
      data: ['策略净值', '基准净值'],
      textStyle: { color: '#a0aec0' },
      top: 10,
    },
    grid: { left: '3%', right: '4%', bottom: '12%', containLabel: true },
    xAxis: {
      type: 'category',
      data: dates,
      axisLine: { lineStyle: { color: '#6E7079' } },
      axisLabel: { color: '#a0aec0', rotate: 45 },
      splitLine: { show: false },
    },
    yAxis: {
      type: 'value',
      axisLine: { lineStyle: { color: '#6E7079' } },
      axisLabel: { color: '#a0aec0' },
      splitLine: { lineStyle: { color: '#2a3449', type: 'dashed' } },
    },
    series: [
      {
        name: '策略净值',
        type: 'line',
        data: navs,
        lineStyle: { width: 2 },
        itemStyle: { color: '#10b981' },
        smooth: true,
        showSymbol: false,
      },
      {
        name: '基准净值',
        type: 'line',
        data: benchs,
        lineStyle: { width: 2, type: 'dashed' },
        itemStyle: { color: '#a0aec0' },
        smooth: true,
        showSymbol: false,
      },
    ],
  }

  navCompareChart.setOption(option, true)
}

// === 响应式更新 ===

watch(() => props.dailyReturns, updateReturnDistChart, { deep: true })
watch(() => props.drawdownCurve, updateDrawdownChart, { deep: true })
watch(() => props.navCurve, updateNavCompareChart, { deep: true })

// === 生命周期 ===

onMounted(() => {
  initReturnDistChart()
  initDrawdownChart()
  initNavCompareChart()

  updateReturnDistChart()
  updateDrawdownChart()
  updateNavCompareChart()

  window.addEventListener('resize', onResize)
})

onUnmounted(() => {
  returnDistChart?.dispose()
  drawdownChart?.dispose()
  navCompareChart?.dispose()
  window.removeEventListener('resize', onResize)
})

function onResize() {
  returnDistChart?.resize()
  drawdownChart?.resize()
  navCompareChart?.resize()
}
</script>

<style scoped>
.basic-metrics-tab {
  display: flex;
  flex-direction: column;
  gap: 20px;
  padding: 20px;
}

.chart-card {
  background: var(--panel-bg, #1a2236);
  border-radius: 12px;
  padding: 20px;
  border: 1px solid var(--border, #2a3449);
  transition: all 0.3s ease;
  box-shadow: 0 4px 6px rgba(0, 0, 0, 0.1);
  display: flex;
  flex-direction: column;
  min-height: 0;
}

.chart-card.full-width {
  grid-column: 1 / -1;
}

.chart-card:hover {
  box-shadow: 0 8px 25px rgba(0, 0, 0, 0.2);
  transform: translateY(-2px);
  border-color: #2962ff;
}

.chart-title {
  font-size: 16px;
  font-weight: 600;
  margin-bottom: 16px;
  color: var(--text, #e0e0e0);
  display: flex;
  align-items: center;
  gap: 12px;
  padding-bottom: 12px;
  border-bottom: 1px solid var(--border, #2a3449);
}

.title-icon {
  font-size: 20px;
  width: 32px;
  height: 32px;
  display: flex;
  align-items: center;
  justify-content: center;
  background: rgba(41, 98, 255, 0.1);
  border-radius: 8px;
}

.chart-container {
  height: 300px;
  width: 100%;
}

@media (max-width: 768px) {
  .chart-card {
    padding: 16px;
  }

  .chart-container {
    height: 250px;
  }
}
</style>
