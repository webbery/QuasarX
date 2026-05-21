<!-- app/src/components/review/tabs/SensitivityTab.vue -->
<!-- 敏感性分析 Tab - 参数敏感性、交易成本敏感性、区间敏感性 -->

<template>
  <div class="sensitivity-tab">
    <!-- 参数敏感性分析 -->
    <div v-if="paramSensitivity.length > 0" class="chart-card">
      <div class="chart-title">
        <div class="title-icon">🔧</div>
        <span>参数敏感性分析</span>
      </div>
      <div class="param-selector">
        <label>选择参数：</label>
        <select v-model="selectedParamIndex">
          <option v-for="(param, idx) in paramSensitivity" :key="idx" :value="idx">
            {{ param.paramName }}
          </option>
        </select>
      </div>
      <div class="chart-container" ref="paramSensitivityRef"></div>
    </div>

    <!-- 交易成本敏感性 -->
    <div v-if="costSensitivity.length > 0" class="chart-card">
      <div class="chart-title">
        <div class="title-icon">💸</div>
        <span>交易成本敏感性</span>
      </div>
      <div class="chart-container" ref="costSensitivityRef"></div>
    </div>

    <!-- 成本敏感性表格 -->
    <div v-if="costSensitivity.length > 0" class="chart-card">
      <div class="chart-title">
        <div class="title-icon">📊</div>
        <span>成本敏感性明细</span>
      </div>
      <div class="cost-table">
        <table class="data-table">
          <thead>
            <tr>
              <th>佣金率</th>
              <th>滑点率</th>
              <th>总收益率</th>
              <th>夏普比率</th>
              <th>交易次数</th>
              <th>收益衰减</th>
            </tr>
          </thead>
          <tbody>
            <tr v-for="(item, idx) in costSensitivity" :key="idx">
              <td>{{ (item.commissionRate * 100).toFixed(2) }}%</td>
              <td>{{ (item.slippageRate * 100).toFixed(2) }}%</td>
              <td :class="getReturnClass(item.totalReturn)">
                {{ (item.totalReturn * 100).toFixed(2) }}%
              </td>
              <td>{{ item.sharpeRatio.toFixed(2) }}</td>
              <td>{{ item.numTrades }}</td>
              <td :class="getDecayClass(idx)">
                {{ getDecayPercent(idx) }}
              </td>
            </tr>
          </tbody>
        </table>
      </div>
    </div>

    <!-- 区间敏感性分析 -->
    <div v-if="periodSensitivity.length > 0" class="chart-card">
      <div class="chart-title">
        <div class="title-icon">📅</div>
        <span>区间敏感性分析</span>
      </div>
      <div class="chart-container" ref="periodSensitivityRef"></div>
    </div>

    <!-- 区间敏感性表格 -->
    <div v-if="periodSensitivity.length > 0" class="chart-card">
      <div class="chart-title">
        <div class="title-icon">📋</div>
        <span>区间敏感性明细</span>
      </div>
      <div class="period-table">
        <table class="data-table">
          <thead>
            <tr>
              <th>区间</th>
              <th>起始日期</th>
              <th>结束日期</th>
              <th>总收益率</th>
              <th>夏普比率</th>
              <th>最大回撤</th>
            </tr>
          </thead>
          <tbody>
            <tr v-for="(item, idx) in periodSensitivity" :key="idx">
              <td>{{ item.period }}</td>
              <td>{{ item.startDate }}</td>
              <td>{{ item.endDate }}</td>
              <td :class="getReturnClass(item.totalReturn)">
                {{ (item.totalReturn * 100).toFixed(2) }}%
              </td>
              <td>{{ item.sharpeRatio.toFixed(2) }}</td>
              <td class="return-negative">{{ (item.maxDrawdown * 100).toFixed(2) }}%</td>
            </tr>
          </tbody>
        </table>
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, watch, onMounted, onUnmounted } from 'vue'
import * as echarts from 'echarts'

interface ParamSensitivity {
  paramName: string
  values: number[]
  returns: number[]
  sharpeRatios: number[]
  maxDrawdowns: number[]
}

interface CostSensitivity {
  commissionRate: number
  slippageRate: number
  totalReturn: number
  sharpeRatio: number
  numTrades: number
}

interface PeriodSensitivity {
  period: string
  startDate: string
  endDate: string
  totalReturn: number
  sharpeRatio: number
  maxDrawdown: number
}

interface Props {
  paramSensitivity: ParamSensitivity[]
  costSensitivity: CostSensitivity[]
  periodSensitivity: PeriodSensitivity[]
}

const props = defineProps<Props>()

const selectedParamIndex = ref(0)

// === 图表引用 ===
const paramSensitivityRef = ref<HTMLElement | null>(null)
const costSensitivityRef = ref<HTMLElement | null>(null)
const periodSensitivityRef = ref<HTMLElement | null>(null)

let paramSensitivityChart: echarts.EChartsType | null = null
let costSensitivityChart: echarts.EChartsType | null = null
let periodSensitivityChart: echarts.EChartsType | null = null

// === 辅助函数 ===

function getReturnClass(value: number): string {
  return value >= 0 ? 'return-positive' : 'return-negative'
}

function getDecayClass(index: number): string {
  if (index === 0) return 'return-positive'
  const decay = (props.costSensitivity[0].totalReturn - props.costSensitivity[index].totalReturn) / props.costSensitivity[0].totalReturn
  return decay > 0.2 ? 'return-negative' : 'return-positive'
}

function getDecayPercent(index: number): string {
  if (index === 0) return '基准'
  const decay = (props.costSensitivity[0].totalReturn - props.costSensitivity[index].totalReturn) / props.costSensitivity[0].totalReturn * 100
  return `-${decay.toFixed(1)}%`
}

// === 参数敏感性图表 ===

function initParamSensitivityChart() {
  if (!paramSensitivityRef.value) return
  paramSensitivityChart = echarts.init(paramSensitivityRef.value, 'quasarx-dark')
}

function updateParamSensitivityChart() {
  if (!paramSensitivityChart || props.paramSensitivity.length === 0) return

  const param = props.paramSensitivity[selectedParamIndex.value]
  if (!param) return

  const option = {
    tooltip: {
      trigger: 'axis',
      backgroundColor: 'rgba(26, 34, 54, 0.9)',
      borderColor: '#2a3449',
      textStyle: { color: '#e0e0e0' },
    },
    legend: {
      data: ['总收益率', '夏普比率', '最大回撤'],
      textStyle: { color: '#a0aec0' },
      top: 10,
    },
    grid: { left: '3%', right: '4%', bottom: '15%', containLabel: true },
    xAxis: {
      type: 'category',
      data: param.values.map(v => v.toString()),
      name: param.paramName,
      nameLocation: 'middle',
      nameGap: 30,
      nameTextStyle: { color: '#a0aec0', fontSize: 12 },
      axisLine: { lineStyle: { color: '#6E7079' } },
      axisLabel: { color: '#a0aec0' },
      splitLine: { show: false },
    },
    yAxis: [
      {
        type: 'value',
        name: '收益率 / 回撤',
        axisLine: { lineStyle: { color: '#6E7079' } },
        axisLabel: { color: '#a0aec0', formatter: '{value}%' },
        splitLine: { lineStyle: { color: '#2a3449', type: 'dashed' } },
      },
      {
        type: 'value',
        name: '夏普比率',
        axisLine: { lineStyle: { color: '#6E7079' } },
        axisLabel: { color: '#a0aec0' },
        splitLine: { show: false },
      },
    ],
    series: [
      {
        name: '总收益率',
        type: 'line',
        data: param.returns.map(v => ({
          value: v * 100,
          itemStyle: { color: '#2962ff' },
        })),
        yAxisIndex: 0,
        smooth: true,
        symbol: 'circle',
        symbolSize: 8,
        lineStyle: { width: 3 },
      },
      {
        name: '夏普比率',
        type: 'line',
        data: param.sharpeRatios.map(v => ({
          value: v,
          itemStyle: { color: '#10b981' },
        })),
        yAxisIndex: 1,
        smooth: true,
        symbol: 'diamond',
        symbolSize: 8,
        lineStyle: { width: 3 },
      },
      {
        name: '最大回撤',
        type: 'line',
        data: param.maxDrawdowns.map(v => ({
          value: v * 100,
          itemStyle: { color: '#ff1744' },
        })),
        yAxisIndex: 0,
        smooth: true,
        symbol: 'triangle',
        symbolSize: 8,
        lineStyle: { width: 2, type: 'dashed' },
      },
    ],
  }

  paramSensitivityChart.setOption(option, true)
}

// === 成本敏感性图表 ===

function initCostSensitivityChart() {
  if (!costSensitivityRef.value) return
  costSensitivityChart = echarts.init(costSensitivityRef.value, 'quasarx-dark')
}

function updateCostSensitivityChart() {
  if (!costSensitivityChart || props.costSensitivity.length === 0) return

  const xLabels = props.costSensitivity.map((_, i) => `场景${i + 1}`)
  const returns = props.costSensitivity.map(c => c.totalReturn * 100)
  const sharpeRatios = props.costSensitivity.map(c => c.sharpeRatio)

  const option = {
    tooltip: {
      trigger: 'axis',
      backgroundColor: 'rgba(26, 34, 54, 0.9)',
      borderColor: '#2a3449',
      textStyle: { color: '#e0e0e0' },
    },
    legend: {
      data: ['总收益率', '夏普比率'],
      textStyle: { color: '#a0aec0' },
      top: 10,
    },
    grid: { left: '3%', right: '4%', bottom: '15%', containLabel: true },
    xAxis: {
      type: 'category',
      data: xLabels,
      axisLine: { lineStyle: { color: '#6E7079' } },
      axisLabel: { color: '#a0aec0' },
      splitLine: { show: false },
    },
    yAxis: [
      {
        type: 'value',
        name: '收益率',
        axisLine: { lineStyle: { color: '#6E7079' } },
        axisLabel: { color: '#a0aec0', formatter: '{value}%' },
        splitLine: { lineStyle: { color: '#2a3449', type: 'dashed' } },
      },
      {
        type: 'value',
        name: '夏普比率',
        axisLine: { lineStyle: { color: '#6E7079' } },
        axisLabel: { color: '#a0aec0' },
        splitLine: { show: false },
      },
    ],
    series: [
      {
        name: '总收益率',
        type: 'bar',
        data: returns.map(v => ({
          value: v,
          itemStyle: { color: v >= 0 ? '#2962ff' : '#ff1744' },
        })),
        label: {
          show: true,
          position: 'top',
          formatter: '{c}%',
          color: '#e0e0e0',
          fontSize: 11,
        },
      },
      {
        name: '夏普比率',
        type: 'line',
        data: sharpeRatios,
        yAxisIndex: 1,
        itemStyle: { color: '#10b981' },
        lineStyle: { width: 3 },
        symbol: 'circle',
        symbolSize: 8,
      },
    ],
  }

  costSensitivityChart.setOption(option, true)
}

// === 区间敏感性图表 ===

function initPeriodSensitivityChart() {
  if (!periodSensitivityRef.value) return
  periodSensitivityChart = echarts.init(periodSensitivityRef.value, 'quasarx-dark')
}

function updatePeriodSensitivityChart() {
  if (!periodSensitivityChart || props.periodSensitivity.length === 0) return

  const periods = props.periodSensitivity.map(p => p.period)
  const returns = props.periodSensitivity.map(p => p.totalReturn * 100)
  const sharpeRatios = props.periodSensitivity.map(p => p.sharpeRatio)
  const drawdowns = props.periodSensitivity.map(p => p.maxDrawdown * 100)

  const option = {
    tooltip: {
      trigger: 'axis',
      backgroundColor: 'rgba(26, 34, 54, 0.9)',
      borderColor: '#2a3449',
      textStyle: { color: '#e0e0e0' },
    },
    legend: {
      data: ['总收益率', '夏普比率', '最大回撤'],
      textStyle: { color: '#a0aec0' },
      top: 10,
    },
    grid: { left: '3%', right: '4%', bottom: '15%', containLabel: true },
    xAxis: {
      type: 'category',
      data: periods,
      axisLine: { lineStyle: { color: '#6E7079' } },
      axisLabel: { color: '#a0aec0', rotate: 30 },
      splitLine: { show: false },
    },
    yAxis: [
      {
        type: 'value',
        name: '收益率 / 回撤',
        axisLine: { lineStyle: { color: '#6E7079' } },
        axisLabel: { color: '#a0aec0', formatter: '{value}%' },
        splitLine: { lineStyle: { color: '#2a3449', type: 'dashed' } },
      },
      {
        type: 'value',
        name: '夏普比率',
        axisLine: { lineStyle: { color: '#6E7079' } },
        axisLabel: { color: '#a0aec0' },
        splitLine: { show: false },
      },
    ],
    series: [
      {
        name: '总收益率',
        type: 'bar',
        data: returns.map(v => ({
          value: v,
          itemStyle: { color: v >= 0 ? '#2962ff' : '#ff1744' },
        })),
        label: {
          show: true,
          position: 'top',
          formatter: '{c}%',
          color: '#e0e0e0',
          fontSize: 11,
        },
      },
      {
        name: '夏普比率',
        type: 'line',
        data: sharpeRatios,
        yAxisIndex: 1,
        itemStyle: { color: '#10b981' },
        lineStyle: { width: 3 },
        symbol: 'circle',
        symbolSize: 8,
      },
      {
        name: '最大回撤',
        type: 'line',
        data: drawdowns,
        itemStyle: { color: '#ff1744' },
        lineStyle: { width: 2, type: 'dashed' },
        symbol: 'triangle',
        symbolSize: 6,
      },
    ],
  }

  periodSensitivityChart.setOption(option, true)
}

// === 响应式更新 ===

watch(selectedParamIndex, updateParamSensitivityChart)
watch(() => props.paramSensitivity, updateParamSensitivityChart, { deep: true })
watch(() => props.costSensitivity, updateCostSensitivityChart, { deep: true })
watch(() => props.periodSensitivity, updatePeriodSensitivityChart, { deep: true })

// === 生命周期 ===

onMounted(() => {
  initParamSensitivityChart()
  initCostSensitivityChart()
  initPeriodSensitivityChart()

  updateParamSensitivityChart()
  updateCostSensitivityChart()
  updatePeriodSensitivityChart()

  window.addEventListener('resize', onResize)
})

onUnmounted(() => {
  paramSensitivityChart?.dispose()
  costSensitivityChart?.dispose()
  periodSensitivityChart?.dispose()
  window.removeEventListener('resize', onResize)
})

function onResize() {
  paramSensitivityChart?.resize()
  costSensitivityChart?.resize()
  periodSensitivityChart?.resize()
}
</script>

<style scoped>
.sensitivity-tab {
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

/* === 参数选择器 === */

.param-selector {
  display: flex;
  align-items: center;
  gap: 12px;
  margin-bottom: 16px;
  padding: 12px;
  background: rgba(42, 52, 77, 0.3);
  border-radius: 8px;
}

.param-selector label {
  font-weight: 600;
  color: #94a3b8;
  font-size: 13px;
}

.param-selector select {
  padding: 6px 12px;
  background: rgba(15, 23, 42, 0.7);
  border: 1px solid rgba(74, 158, 255, 0.3);
  border-radius: 8px;
  color: #e2e8f0;
  font-size: 14px;
  min-width: 150px;
}

/* === 表格 === */

.cost-table,
.period-table {
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
