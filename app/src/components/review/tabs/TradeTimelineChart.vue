<!-- app/src/components/review/tabs/TradeTimelineChart.vue -->
<!-- 交易时序图 - 聚焦单个标的的成交价买卖点 + 单笔盈亏子面板 + 各标的总收益子图 -->

<template>
  <div class="timeline-chart">
    <!-- 标的切换 -->
    <div class="chart-toolbar">
      <label for="trade-symbol-select">标的：</label>
      <select id="trade-symbol-select" v-model="selectedSymbol" class="symbol-select">
        <option v-for="s in symbolList" :key="s" :value="s">{{ s }}</option>
      </select>
      <span class="toolbar-hint">
        {{ symbolTrades.length }} 笔成交 · 本标的累计盈亏
        <span :class="symbolPnl >= 0 ? 'positive' : 'negative'">
          {{ symbolPnl >= 0 ? '+' : '' }}¥{{ symbolPnl.toFixed(2) }}
        </span>
      </span>
    </div>

    <!-- 成交价与买卖点 / 单笔盈亏 -->
    <div class="chart-container" ref="mainRef"></div>

    <!-- 各标的总收益（容器常驻，避免数据到达时图表无法初始化） -->
    <div class="sub-chart-section">
      <div class="sub-chart-title">
        <span class="sub-icon">📊</span>
        <span>各标的总收益</span>
        <span class="sub-hint">点击柱状可切换上方标的</span>
      </div>
      <div class="sub-chart-container" ref="subRef" :style="{ height: subChartHeight + 'px' }"></div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, computed, watch, onMounted, onUnmounted } from 'vue'
import * as echarts from 'echarts'
import { registerDarkTheme } from '@/lib/echartsTheme'
import type { StrategyTrade } from '@/composables/useStrategyTrades'

interface Props {
  trades: StrategyTrade[]
}

const props = defineProps<Props>()

const GREEN = '#10b981'
const RED = '#ff1744'
const BLUE = '#60a5fa'

// === 状态 ===
const selectedSymbol = ref('')
const mainRef = ref<HTMLElement | null>(null)
const subRef = ref<HTMLElement | null>(null)

let mainChart: echarts.EChartsType | null = null
let subChart: echarts.EChartsType | null = null

// === 计算属性 ===

/** 标的清单（按成交笔数降序） */
const symbolList = computed(() => {
  const counter = new Map<string, number>()
  props.trades.forEach(t => counter.set(t.symbol, (counter.get(t.symbol) ?? 0) + 1))
  return [...counter.entries()]
    .sort((a, b) => b[1] - a[1] || a[0].localeCompare(b[0]))
    .map(([symbol]) => symbol)
})

/** 当前标的的成交明细 */
const symbolTrades = computed(() =>
  props.trades
    .filter(t => t.symbol === selectedSymbol.value)
    .sort((a, b) => a.timestamp.localeCompare(b.timestamp))
)

/** 当前标的累计盈亏 */
const symbolPnl = computed(() => symbolTrades.value.reduce((sum, t) => sum + (t.pnl ?? 0), 0))

/** 各标的总收益（降序） */
const symbolPnlRows = computed(() => {
  const map = new Map<string, number>()
  props.trades.forEach(t => {
    if (t.pnl === undefined) return
    map.set(t.symbol, (map.get(t.symbol) ?? 0) + t.pnl)
  })
  return [...map.entries()]
    .map(([symbol, pnl]) => ({ symbol, pnl }))
    .sort((a, b) => b.pnl - a.pnl)
})

/** 子图高度随标的数量自适应 */
const subChartHeight = computed(() =>
  Math.min(340, Math.max(120, symbolPnlRows.value.length * 26 + 40))
)

// 标的清单变化时保证选中项有效
watch(symbolList, (list) => {
  if (list.length === 0) {
    selectedSymbol.value = ''
  } else if (!list.includes(selectedSymbol.value)) {
    selectedSymbol.value = list[0]
  }
}, { immediate: true })

// === 主图：成交价买卖点 + 单笔盈亏 ===

function initMainChart() {
  if (!mainRef.value) return
  registerDarkTheme()
  mainChart = echarts.init(mainRef.value, 'quasarx-dark')
}

function buildMainOption(): any {
  const trades = symbolTrades.value
  const labels = trades.map(t => (t.timestamp ? t.timestamp.slice(5) : ''))
  const buyPrices = trades.map(t => (t.direction === 'buy' ? t.price : null))
  const sellPrices = trades.map(t => (t.direction === 'sell' ? t.price : null))
  const pnlBars = trades.map(t => ({
    value: t.pnl !== undefined ? t.pnl : null,
    itemStyle: { color: (t.pnl ?? 0) >= 0 ? 'rgba(16, 185, 129, 0.85)' : 'rgba(255, 23, 68, 0.85)' },
  }))

  return {
    tooltip: {
      trigger: 'axis',
      axisPointer: { type: 'shadow' },
      backgroundColor: 'rgba(26, 34, 54, 0.94)',
      borderColor: '#2a3449',
      textStyle: { color: '#e0e0e0' },
      formatter: (params: any) => {
        const first = Array.isArray(params) ? params[0] : params
        const t = trades[first.dataIndex]
        if (!t) return ''
        const dirText = t.direction === 'buy' ? '买入' : '卖出'
        const dirColor = t.direction === 'buy' ? GREEN : RED
        const pnlText = t.pnl !== undefined ? `¥${t.pnl.toFixed(2)}` : '-'
        const pnlColor = t.pnl === undefined ? '#a0aec0' : (t.pnl >= 0 ? GREEN : RED)
        return [
          `${t.timestamp}　${t.symbol}`,
          `方向: <span style="color:${dirColor}; font-weight: bold;">${dirText}</span>`,
          `价格: ¥${t.price.toFixed(2)}　数量: ${t.quantity}`,
          `金额: ¥${t.amount.toFixed(2)}　佣金: ¥${t.commission.toFixed(2)}`,
          `盈亏: <span style="color:${pnlColor}; font-weight: bold;">${pnlText}</span>`,
        ].join('<br/>')
      },
    },
    // 上下子面板共享同一时间轴，悬浮联动
    axisPointer: { link: [{ xAxisIndex: 'all' }] },
    legend: {
      data: ['成交价', '买入', '卖出'],
      textStyle: { color: '#a0aec0' },
      top: 0,
      right: 0,
    },
    grid: [
      { left: '3%', right: '5%', top: 34, height: '46%', containLabel: true },
      { left: '3%', right: '5%', top: '72%', height: '18%', containLabel: true },
    ],
    xAxis: [
      {
        type: 'category',
        gridIndex: 0,
        data: labels,
        axisLine: { lineStyle: { color: '#6E7079' } },
        axisLabel: { show: false },
        splitLine: { show: false },
      },
      {
        type: 'category',
        gridIndex: 1,
        data: labels,
        axisLine: { lineStyle: { color: '#6E7079' } },
        axisLabel: { color: '#a0aec0', rotate: 45, hideOverlap: true },
        splitLine: { show: false },
      },
    ],
    yAxis: [
      {
        type: 'value',
        gridIndex: 0,
        name: '成交价 (¥)',
        scale: true,
        axisLine: { lineStyle: { color: '#6E7079' } },
        axisLabel: { color: '#a0aec0', formatter: '¥{value}' },
        splitLine: { lineStyle: { color: '#2a3449', type: 'dashed' } },
      },
      {
        type: 'value',
        gridIndex: 1,
        name: '单笔盈亏 (¥)',
        position: 'right',
        axisLine: { lineStyle: { color: '#6E7079' } },
        axisLabel: { color: '#a0aec0', formatter: '¥{value}' },
        splitLine: { lineStyle: { color: '#2a3449', type: 'dashed' } },
      },
    ],
    series: [
      {
        name: '成交价',
        type: 'line',
        xAxisIndex: 0,
        yAxisIndex: 0,
        data: trades.map(t => t.price),
        showSymbol: false,
        smooth: false,
        z: 2,
        lineStyle: { width: 2, color: BLUE },
        itemStyle: { color: BLUE },
        areaStyle: {
          color: new echarts.graphic.LinearGradient(0, 0, 0, 1, [
            { offset: 0, color: 'rgba(96, 165, 250, 0.18)' },
            { offset: 1, color: 'rgba(96, 165, 250, 0.01)' },
          ]),
        },
      },
      {
        name: '买入',
        type: 'scatter',
        xAxisIndex: 0,
        yAxisIndex: 0,
        data: buyPrices,
        z: 5,
        symbol: 'triangle',
        symbolSize: 17,
        itemStyle: { color: GREEN, borderColor: '#fff', borderWidth: 1.5 },
        label: {
          show: true,
          formatter: '买',
          position: 'top',
          distance: 7,
          color: GREEN,
          fontWeight: 'bold',
          fontSize: 12,
        },
        labelLayout: { hideOverlap: true },
      },
      {
        name: '卖出',
        type: 'scatter',
        xAxisIndex: 0,
        yAxisIndex: 0,
        data: sellPrices,
        z: 5,
        symbol: 'triangle',
        symbolRotate: 180,
        symbolSize: 17,
        itemStyle: { color: RED, borderColor: '#fff', borderWidth: 1.5 },
        label: {
          show: true,
          formatter: '卖',
          position: 'bottom',
          distance: 7,
          color: RED,
          fontWeight: 'bold',
          fontSize: 12,
        },
        labelLayout: { hideOverlap: true },
      },
      {
        name: '单笔盈亏',
        type: 'bar',
        xAxisIndex: 1,
        yAxisIndex: 1,
        data: pnlBars,
        barMaxWidth: 20,
        markLine: {
          silent: true,
          symbol: 'none',
          data: [{ yAxis: 0 }],
          lineStyle: { color: '#64748b', type: 'dashed', width: 1 },
        },
      },
    ],
  }
}

function updateMainChart() {
  if (!mainChart) return
  if (symbolTrades.value.length === 0) {
    mainChart.clear()
    return
  }
  mainChart.setOption(buildMainOption(), true)
}

// === 子图：各标的总收益 ===

function initSubChart() {
  if (!subRef.value) return
  registerDarkTheme()
  subChart = echarts.init(subRef.value, 'quasarx-dark')
  subChart.on('click', (params: any) => {
    if (params.componentType === 'series' && params.name) {
      selectedSymbol.value = params.name
    }
  })
}

function buildSubOption(): any {
  const rows = symbolPnlRows.value
  const option: any = {
    tooltip: {
      trigger: 'item',
      backgroundColor: 'rgba(26, 34, 54, 0.94)',
      borderColor: '#2a3449',
      textStyle: { color: '#e0e0e0' },
      formatter: (params: any) => {
        const value = Number(params.value) || 0
        const color = value >= 0 ? GREEN : RED
        return [
          `<b>${params.name}</b>`,
          `总收益: <span style="color:${color}; font-weight: bold;">${value >= 0 ? '+' : ''}¥${value.toFixed(2)}</span>`,
          `<span style="color:#64748b;">点击切换上方标的</span>`,
        ].join('<br/>')
      },
    },
    grid: { left: 78, right: 78, top: 8, bottom: 26 },
    xAxis: {
      type: 'value',
      axisLine: { lineStyle: { color: '#6E7079' } },
      axisLabel: { color: '#a0aec0', formatter: (v: number) => `¥${Math.round(v)}` },
      splitLine: { lineStyle: { color: '#2a3449', type: 'dashed' } },
    },
    yAxis: {
      type: 'category',
      data: rows.map(r => r.symbol),
      inverse: true,
      axisLine: { lineStyle: { color: '#6E7079' } },
      axisLabel: { color: '#e2e8f0', fontSize: 12, fontWeight: 'bold' },
      splitLine: { show: false },
    },
    series: [
      {
        name: '总收益',
        type: 'bar',
        barMaxWidth: 20,
        data: rows.map(r => ({
          value: r.pnl,
          itemStyle: {
            color: r.pnl >= 0 ? 'rgba(16, 185, 129, 0.85)' : 'rgba(255, 23, 68, 0.85)',
            borderColor: r.symbol === selectedSymbol.value ? '#fff' : 'transparent',
            borderWidth: r.symbol === selectedSymbol.value ? 1.5 : 0,
          },
        })),
        label: {
          show: true,
          position: 'right',
          distance: 6,
          color: '#cbd5e1',
          fontSize: 11,
          formatter: (p: any) => `${Number(p.value) >= 0 ? '+' : ''}${Number(p.value).toFixed(0)}`,
        },
      },
    ],
  }

  if (rows.length === 0) {
    option.title = {
      text: '暂无数据',
      left: 'center',
      top: 'middle',
      textStyle: { color: '#64748b', fontSize: 13, fontWeight: 'normal' },
    }
  }

  return option
}

function updateSubChart() {
  if (!subChart) return
  subChart.setOption(buildSubOption(), true)
}

// === 响应式更新 ===

watch(symbolTrades, () => {
  updateMainChart()
}, { deep: true })

watch(symbolPnlRows, () => {
  updateSubChart()
}, { deep: true })

// 切换标的时同步刷新两个图
watch(selectedSymbol, () => {
  updateMainChart()
  updateSubChart()
})

// === 生命周期 ===

onMounted(() => {
  initMainChart()
  initSubChart()
  updateMainChart()
  updateSubChart()
  window.addEventListener('resize', onResize)
})

onUnmounted(() => {
  mainChart?.dispose()
  subChart?.dispose()
  window.removeEventListener('resize', onResize)
})

function onResize() {
  mainChart?.resize()
  subChart?.resize()
}
</script>

<style scoped>
.timeline-chart {
  display: flex;
  flex-direction: column;
  gap: 12px;
}

/* === 工具栏 === */

.chart-toolbar {
  display: flex;
  align-items: center;
  gap: 10px;
}

.chart-toolbar label {
  font-size: 13px;
  color: #94a3b8;
}

.symbol-select {
  padding: 5px 10px;
  background: rgba(15, 23, 42, 0.9);
  border: 1px solid rgba(74, 158, 255, 0.3);
  border-radius: 6px;
  color: #e2e8f0;
  font-size: 13px;
  cursor: pointer;
  font-family: 'SF Mono', 'Fira Code', monospace;
}

.symbol-select:focus {
  outline: none;
  border-color: rgba(74, 158, 255, 0.6);
}

.toolbar-hint {
  font-size: 12px;
  color: #64748b;
}

.positive {
  color: #10b981;
  font-weight: 600;
}

.negative {
  color: #ff1744;
  font-weight: 600;
}

/* === 图表 === */

.chart-container {
  width: 100%;
  height: 420px;
}

/* === 子图 === */

.sub-chart-section {
  border-top: 1px solid var(--border, #2a3449);
  padding-top: 12px;
}

.sub-chart-title {
  display: flex;
  align-items: center;
  gap: 8px;
  font-size: 14px;
  font-weight: 600;
  color: var(--text, #e0e0e0);
  margin-bottom: 8px;
}

.sub-icon {
  font-size: 16px;
}

.sub-hint {
  font-size: 12px;
  font-weight: 400;
  color: #64748b;
}

.sub-chart-container {
  width: 100%;
}
</style>
