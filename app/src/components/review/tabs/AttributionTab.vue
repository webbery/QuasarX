<!-- app/src/components/review/tabs/AttributionTab.vue -->
<!-- 归因分析 Tab - 收益来源拆解、分时段收益、行业/风格暴露 -->

<template>
  <div class="attribution-tab">
    <!-- 收益归因瀑布图 -->
    <div v-if="attributionData" class="chart-card">
      <div class="chart-title">
        <div class="title-icon">🎯</div>
        <span>收益归因分解</span>
      </div>
      <div class="chart-container" ref="attributionRef"></div>
    </div>

    <!-- 行业贡献度 -->
    <div v-if="attributionData" class="chart-card">
      <div class="chart-title">
        <div class="title-icon">🏭</div>
        <span>行业收益贡献</span>
      </div>
      <div class="sector-table">
        <table class="data-table">
          <thead>
            <tr>
              <th>行业</th>
              <th>权重</th>
              <th>行业收益</th>
              <th>收益贡献</th>
              <th>贡献占比</th>
            </tr>
          </thead>
          <tbody>
            <tr v-for="sector in sortedSectors" :key="sector.name">
              <td>{{ sector.name }}</td>
              <td>{{ (sector.weight * 100).toFixed(1) }}%</td>
              <td :class="getReturnClass(sector.return)">
                {{ (sector.return * 100).toFixed(2) }}%
              </td>
              <td :class="getReturnClass(sector.contribution)">
                {{ (sector.contribution * 100).toFixed(2) }}%
              </td>
              <td>
                <div class="contribution-bar">
                  <div
                    class="bar-fill"
                    :style="{
                      width: Math.abs(getContributionPercent(sector)) + '%',
                      backgroundColor: sector.contribution >= 0 ? '#10b981' : '#ff1744'
                    }"
                  ></div>
                  <span>{{ getContributionPercent(sector).toFixed(1) }}%</span>
                </div>
              </td>
            </tr>
          </tbody>
        </table>
      </div>
    </div>

    <!-- 分时段收益对比 -->
    <div v-if="periodReturns.length > 0" class="chart-card">
      <div class="chart-title">
        <div class="title-icon">📅</div>
        <span>分时段收益对比</span>
      </div>
      <div class="chart-container" ref="periodReturnRef"></div>
    </div>

    <!-- 风格因子暴露 -->
    <div v-if="styleExposure.length > 0" class="chart-card">
      <div class="chart-title">
        <div class="title-icon">⚖️</div>
        <span>风格因子暴露与贡献</span>
      </div>
      <div class="chart-container" ref="styleExposureRef"></div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, watch, computed, onMounted, onUnmounted } from 'vue'
import * as echarts from 'echarts'

interface SectorBreakdown {
  name: string
  weight: number
  return: number
  contribution: number
}

interface AttributionData {
  totalReturn: number
  selectionEffect: number
  allocationEffect: number
  interactionEffect: number
  sectorBreakdown: SectorBreakdown[]
}

interface PeriodReturn {
  period: string
  strategyReturn: number
  benchmarkReturn: number
  excessReturn: number
}

interface StyleExposure {
  factor: string
  exposure: number
  contribution: number
}

interface Props {
  attributionData: AttributionData | null
  periodReturns: PeriodReturn[]
  styleExposure: StyleExposure[]
}

const props = defineProps<Props>()

// === 图表引用 ===
const attributionRef = ref<HTMLElement | null>(null)
const periodReturnRef = ref<HTMLElement | null>(null)
const styleExposureRef = ref<HTMLElement | null>(null)

let attributionChart: echarts.EChartsType | null = null
let periodReturnChart: echarts.EChartsType | null = null
let styleExposureChart: echarts.EChartsType | null = null

// === 计算属性 ===

const sortedSectors = computed(() => {
  if (!props.attributionData) return []
  return [...props.attributionData.sectorBreakdown].sort((a, b) => b.contribution - a.contribution)
})

function getContributionPercent(sector: SectorBreakdown): number {
  if (!props.attributionData || props.attributionData.totalReturn === 0) return 0
  return (sector.contribution / props.attributionData.totalReturn) * 100
}

function getReturnClass(value: number): string {
  return value >= 0 ? 'return-positive' : 'return-negative'
}

// === 收益归因瀑布图 ===

function initAttributionChart() {
  if (!attributionRef.value) return
  attributionChart = echarts.init(attributionRef.value, 'quasarx-dark')
}

function updateAttributionChart() {
  if (!attributionChart || !props.attributionData) return

  const { totalReturn, selectionEffect, allocationEffect, interactionEffect } = props.attributionData

  const categories = ['选股效应', '配置效应', '交互效应', '总收益']
  const values = [selectionEffect, allocationEffect, interactionEffect, totalReturn]
  const colors = values.map(v => v >= 0 ? '#10b981' : '#ff1744')

  const option = {
    tooltip: {
      trigger: 'axis',
      backgroundColor: 'rgba(26, 34, 54, 0.9)',
      borderColor: '#2a3449',
      textStyle: { color: '#e0e0e0' },
      formatter: (params: any) => {
        const val = params[0].value
        return `${params[0].name}<br/>贡献: <span style="color: ${val >= 0 ? '#10b981' : '#ff1744'}; font-weight: bold;">${(val * 100).toFixed(2)}%</span>`
      },
    },
    grid: { left: '3%', right: '4%', bottom: '15%', containLabel: true },
    xAxis: {
      type: 'category',
      data: categories,
      axisLine: { lineStyle: { color: '#6E7079' } },
      axisLabel: { color: '#a0aec0' },
      splitLine: { show: false },
    },
    yAxis: {
      type: 'value',
      axisLine: { lineStyle: { color: '#6E7079' } },
      axisLabel: { color: '#a0aec0', formatter: (v: number) => (v * 100).toFixed(1) + '%' },
      splitLine: { lineStyle: { color: '#2a3449', type: 'dashed' } },
    },
    series: [{
      name: '贡献度',
      type: 'bar',
      data: values.map((v, i) => ({
        value: v,
        itemStyle: { color: colors[i] },
      })),
      label: {
        show: true,
        position: 'top',
        formatter: (p: any) => (p.value * 100).toFixed(2) + '%',
        color: '#e0e0e0',
      },
    }],
  }

  attributionChart.setOption(option, true)
}

// === 分时段收益对比图 ===

function initPeriodReturnChart() {
  if (!periodReturnRef.value) return
  periodReturnChart = echarts.init(periodReturnRef.value, 'quasarx-dark')
}

function updatePeriodReturnChart() {
  if (!periodReturnChart || props.periodReturns.length === 0) return

  const periods = props.periodReturns.map(p => p.period)
  const strategyReturns = props.periodReturns.map(p => p.strategyReturn * 100)
  const benchmarkReturns = props.periodReturns.map(p => p.benchmarkReturn * 100)
  const excessReturns = props.periodReturns.map(p => p.excessReturn * 100)

  const option = {
    tooltip: {
      trigger: 'axis',
      backgroundColor: 'rgba(26, 34, 54, 0.9)',
      borderColor: '#2a3449',
      textStyle: { color: '#e0e0e0' },
    },
    legend: {
      data: ['策略收益', '基准收益', '超额收益'],
      textStyle: { color: '#a0aec0' },
      top: 10,
    },
    grid: { left: '3%', right: '4%', bottom: '15%', containLabel: true },
    xAxis: {
      type: 'category',
      data: periods,
      axisLine: { lineStyle: { color: '#6E7079' } },
      axisLabel: { color: '#a0aec0' },
      splitLine: { show: false },
    },
    yAxis: {
      type: 'value',
      axisLine: { lineStyle: { color: '#6E7079' } },
      axisLabel: { color: '#a0aec0', formatter: '{value}%' },
      splitLine: { lineStyle: { color: '#2a3449', type: 'dashed' } },
    },
    series: [
      {
        name: '策略收益',
        type: 'bar',
        data: strategyReturns,
        itemStyle: { color: '#2962ff' },
      },
      {
        name: '基准收益',
        type: 'bar',
        data: benchmarkReturns,
        itemStyle: { color: '#a0aec0' },
      },
      {
        name: '超额收益',
        type: 'line',
        data: excessReturns,
        itemStyle: { color: '#10b981' },
        lineStyle: { width: 3 },
        symbol: 'circle',
        symbolSize: 6,
      },
    ],
  }

  periodReturnChart.setOption(option, true)
}

// === 风格因子暴露图 ===

function initStyleExposureChart() {
  if (!styleExposureRef.value) return
  styleExposureChart = echarts.init(styleExposureRef.value, 'quasarx-dark')
}

function updateStyleExposureChart() {
  if (!styleExposureChart || props.styleExposure.length === 0) return

  const factors = props.styleExposure.map(s => s.factor)
  const exposures = props.styleExposure.map(s => s.exposure)
  const contributions = props.styleExposure.map(s => s.contribution)

  const option = {
    tooltip: {
      trigger: 'axis',
      backgroundColor: 'rgba(26, 34, 54, 0.9)',
      borderColor: '#2a3449',
      textStyle: { color: '#e0e0e0' },
    },
    legend: {
      data: ['因子暴露', '收益贡献'],
      textStyle: { color: '#a0aec0' },
      top: 10,
    },
    grid: { left: '3%', right: '4%', bottom: '15%', containLabel: true },
    xAxis: {
      type: 'category',
      data: factors,
      axisLine: { lineStyle: { color: '#6E7079' } },
      axisLabel: { color: '#a0aec0' },
      splitLine: { show: false },
    },
    yAxis: [
      {
        type: 'value',
        name: '暴露',
        axisLine: { lineStyle: { color: '#6E7079' } },
        axisLabel: { color: '#a0aec0' },
        splitLine: { lineStyle: { color: '#2a3449', type: 'dashed' } },
      },
      {
        type: 'value',
        name: '贡献',
        axisLine: { lineStyle: { color: '#6E7079' } },
        axisLabel: { color: '#a0aec0' },
        splitLine: { show: false },
      },
    ],
    series: [
      {
        name: '因子暴露',
        type: 'bar',
        data: exposures.map(v => ({
          value: v,
          itemStyle: { color: v >= 0 ? '#2962ff' : '#ff9800' },
        })),
        yAxisIndex: 0,
      },
      {
        name: '收益贡献',
        type: 'line',
        data: contributions.map(v => ({
          value: v,
          itemStyle: { color: v >= 0 ? '#10b981' : '#ff1744' },
        })),
        yAxisIndex: 1,
        lineStyle: { width: 3 },
        symbol: 'circle',
        symbolSize: 8,
      },
    ],
  }

  styleExposureChart.setOption(option, true)
}

// === 响应式更新 ===

watch(() => props.attributionData, updateAttributionChart, { deep: true })
watch(() => props.periodReturns, updatePeriodReturnChart, { deep: true })
watch(() => props.styleExposure, updateStyleExposureChart, { deep: true })

// === 生命周期 ===

onMounted(() => {
  initAttributionChart()
  initPeriodReturnChart()
  initStyleExposureChart()

  updateAttributionChart()
  updatePeriodReturnChart()
  updateStyleExposureChart()

  window.addEventListener('resize', onResize)
})

onUnmounted(() => {
  attributionChart?.dispose()
  periodReturnChart?.dispose()
  styleExposureChart?.dispose()
  window.removeEventListener('resize', onResize)
})

function onResize() {
  attributionChart?.resize()
  periodReturnChart?.resize()
  styleExposureChart?.resize()
}
</script>

<style scoped>
.attribution-tab {
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
  height: 350px;
  width: 100%;
}

/* === 行业表格 === */

.sector-table {
  overflow-x: auto;
}

.data-table {
  width: 100%;
  border-collapse: collapse;
  font-size: 13px;
}

.data-table thead tr {
  border-bottom: 2px solid var(--border, #2a3449);
}

.data-table th {
  padding: 10px 14px;
  text-align: left;
  color: #94a3b8;
  font-weight: 600;
  white-space: nowrap;
}

.data-table tbody tr {
  border-bottom: 1px solid rgba(42, 52, 77, 0.4);
}

.data-table tbody tr:hover {
  background-color: rgba(42, 52, 77, 0.3);
}

.data-table td {
  padding: 10px 14px;
  color: var(--text, #e0e0e0);
  white-space: nowrap;
}

.return-positive {
  color: #10b981 !important;
  font-weight: 600;
}

.return-negative {
  color: #ff1744 !important;
  font-weight: 600;
}

/* === 贡献度进度条 === */

.contribution-bar {
  display: flex;
  align-items: center;
  gap: 8px;
  min-width: 120px;
}

.bar-fill {
  height: 8px;
  border-radius: 4px;
  transition: width 0.3s ease;
}

.contribution-bar span {
  font-size: 12px;
  color: #94a3b8;
  min-width: 45px;
}

@media (max-width: 768px) {
  .chart-card {
    padding: 16px;
  }

  .chart-container {
    height: 280px;
  }

  .data-table {
    font-size: 12px;
  }

  .data-table th,
  .data-table td {
    padding: 8px 10px;
  }
}
</style>
