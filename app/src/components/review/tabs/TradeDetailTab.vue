<!-- app/src/components/review/tabs/TradeDetailTab.vue -->
<!-- 交易明细 Tab - 交易统计、交易时序、盈亏分布、累计盈亏（纯图表展示） -->
<!-- 明细表格已迁移至「下单管理」的 TradeRecordsPanel.vue -->

<template>
  <div class="trade-detail-tab">
    <!-- 交易统计卡片 -->
    <div class="stats-grid">
      <div class="stat-card">
        <div class="stat-label">总交易次数</div>
        <div class="stat-value">{{ tradeStats.totalTrades }}</div>
      </div>
      <div class="stat-card">
        <div class="stat-label">盈利次数</div>
        <div class="stat-value positive">{{ tradeStats.winCount }}</div>
      </div>
      <div class="stat-card">
        <div class="stat-label">亏损次数</div>
        <div class="stat-value negative">{{ tradeStats.lossCount }}</div>
      </div>
      <div class="stat-card">
        <div class="stat-label">胜率</div>
        <div class="stat-value" :class="tradeStats.winRate >= 0.5 ? 'positive' : 'negative'">
          {{ (tradeStats.winRate * 100).toFixed(1) }}%
        </div>
      </div>
      <div class="stat-card">
        <div class="stat-label">平均盈利</div>
        <div class="stat-value positive">¥{{ tradeStats.avgWin.toFixed(2) }}</div>
      </div>
      <div class="stat-card">
        <div class="stat-label">平均亏损</div>
        <div class="stat-value negative">¥{{ tradeStats.avgLoss.toFixed(2) }}</div>
      </div>
    </div>

    <!-- 交易时序与单笔盈亏 -->
    <div v-if="trades.length > 0" class="chart-card">
      <div class="chart-title">
        <div class="title-icon">🕘</div>
        <span>交易时序与单笔盈亏</span>
      </div>
      <TradeTimelineChart :trades="trades" />
    </div>

    <!-- 盈亏分布直方图 -->
    <div class="chart-card">
      <div class="chart-title">
        <div class="title-icon">📊</div>
        <span>盈亏分布</span>
      </div>
      <div class="chart-container" ref="pnlDistRef"></div>
    </div>

    <!-- 累计盈亏曲线 -->
    <div class="chart-card">
      <div class="chart-title">
        <div class="title-icon">📈</div>
        <span>累计盈亏</span>
      </div>
      <div class="chart-container" ref="cumPnlRef"></div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, computed, watch, onMounted, onUnmounted } from 'vue'
import * as echarts from 'echarts'
import { registerDarkTheme } from '@/lib/echartsTheme'
import TradeTimelineChart from './TradeTimelineChart.vue'
import type { StrategyTrade } from '@/composables/useStrategyTrades'

interface Props {
  trades: StrategyTrade[]
}

const props = defineProps<Props>()

// === 图表引用 ===
const pnlDistRef = ref<HTMLElement | null>(null)
const cumPnlRef = ref<HTMLElement | null>(null)

let pnlDistChart: echarts.EChartsType | null = null
let cumPnlChart: echarts.EChartsType | null = null

// === 计算属性 ===

const sortedTrades = computed(() => {
  return [...props.trades].sort((a, b) => a.timestamp.localeCompare(b.timestamp))
})

const tradeStats = computed(() => {
  const tradesWithPnl = props.trades.filter(t => t.pnl !== undefined)
  const wins = tradesWithPnl.filter(t => (t.pnl || 0) > 0)
  const losses = tradesWithPnl.filter(t => (t.pnl || 0) <= 0)

  return {
    totalTrades: props.trades.length,
    winCount: wins.length,
    lossCount: losses.length,
    winRate: tradesWithPnl.length > 0 ? wins.length / tradesWithPnl.length : 0,
    avgWin: wins.length > 0 ? wins.reduce((sum, t) => sum + (t.pnl || 0), 0) / wins.length : 0,
    avgLoss: losses.length > 0 ? Math.abs(losses.reduce((sum, t) => sum + (t.pnl || 0), 0) / losses.length) : 0,
  }
})

// === 盈亏分布图表 ===

function initPnlDistChart() {
  if (!pnlDistRef.value) return
  registerDarkTheme()
  pnlDistChart = echarts.init(pnlDistRef.value, 'quasarx-dark')
}

function updatePnlDistChart() {
  if (!pnlDistChart) return

  const pnls = props.trades.filter(t => t.pnl !== undefined).map(t => t.pnl || 0)
  if (pnls.length === 0) return

  const bins = 30
  const min = Math.min(...pnls)
  const max = Math.max(...pnls)
  const binWidth = (max - min) / bins || 1
  const histogram = new Array(bins).fill(0)

  pnls.forEach(pnl => {
    const binIndex = Math.min(Math.floor((pnl - min) / binWidth), bins - 1)
    histogram[binIndex]++
  })

  const binCenters = histogram.map((_, i) => min + binWidth * (i + 0.5))

  const option = {
    tooltip: {
      trigger: 'axis',
      backgroundColor: 'rgba(26, 34, 54, 0.9)',
      borderColor: '#2a3449',
      textStyle: { color: '#e0e0e0' },
      formatter: (params: any) => {
        const val = params[0].value
        return `盈亏区间: ¥${params[0].axisValue}<br/>频次: <span style="color: #2962ff; font-weight: bold;">${val}</span>`
      },
    },
    grid: { left: '3%', right: '4%', bottom: '12%', containLabel: true },
    xAxis: {
      type: 'category',
      data: binCenters.map(v => v.toFixed(0)),
      axisLine: { lineStyle: { color: '#6E7079' } },
      axisLabel: { color: '#a0aec0' },
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
      data: histogram.map((count, i) => ({
        value: count,
        itemStyle: {
          color: binCenters[i] >= 0 ? '#10b981' : '#ff1744',
        },
      })),
    }],
  }

  pnlDistChart.setOption(option, true)
}

// === 累计盈亏曲线 ===

function initCumPnlChart() {
  if (!cumPnlRef.value) return
  registerDarkTheme()
  cumPnlChart = echarts.init(cumPnlRef.value, 'quasarx-dark')
}

function updateCumPnlChart() {
  if (!cumPnlChart) return

  const trades = sortedTrades.value.filter(t => t.pnl !== undefined)
  if (trades.length === 0) return

  let cumPnl = 0
  const cumPnlData = trades.map(t => {
    cumPnl += t.pnl || 0
    return [t.timestamp, cumPnl]
  })

  const option = {
    tooltip: {
      trigger: 'axis',
      backgroundColor: 'rgba(26, 34, 54, 0.9)',
      borderColor: '#2a3449',
      textStyle: { color: '#e0e0e0' },
      formatter: (params: any) => {
        const ts = params[0].value[0]
        const val = params[0].value[1]
        const dateStr = typeof ts === 'number' ? new Date(ts).toLocaleDateString() : String(ts)
        return `${dateStr}<br/>累计盈亏: <span style="color: ${val >= 0 ? '#10b981' : '#ff1744'}; font-weight: bold;">¥${val.toFixed(2)}</span>`
      },
    },
    grid: { left: '3%', right: '4%', bottom: '12%', containLabel: true },
    xAxis: {
      type: 'time',
      axisLine: { lineStyle: { color: '#6E7079' } },
      axisLabel: { color: '#a0aec0', rotate: 45 },
      splitLine: { show: false },
    },
    yAxis: {
      type: 'value',
      axisLine: { lineStyle: { color: '#6E7079' } },
      axisLabel: { color: '#a0aec0', formatter: '¥{value}' },
      splitLine: { lineStyle: { color: '#2a3449', type: 'dashed' } },
    },
    series: [{
      name: '累计盈亏',
      type: 'line',
      data: cumPnlData,
      lineStyle: { width: 2 },
      itemStyle: { color: '#2962ff' },
      areaStyle: {
        color: new echarts.graphic.LinearGradient(0, 0, 0, 1, [
          { offset: 0, color: 'rgba(41, 98, 255, 0.3)' },
          { offset: 1, color: 'rgba(41, 98, 255, 0.05)' },
        ]),
      },
      smooth: true,
      showSymbol: false,
    }],
  }

  cumPnlChart.setOption(option, true)
}

// === 响应式更新 ===

watch(() => props.trades, () => {
  updatePnlDistChart()
  updateCumPnlChart()
}, { deep: true })

// === 生命周期 ===

onMounted(() => {
  initPnlDistChart()
  initCumPnlChart()

  updatePnlDistChart()
  updateCumPnlChart()

  window.addEventListener('resize', onResize)
})

onUnmounted(() => {
  pnlDistChart?.dispose()
  cumPnlChart?.dispose()
  window.removeEventListener('resize', onResize)
})

function onResize() {
  pnlDistChart?.resize()
  cumPnlChart?.resize()
}
</script>

<style scoped>
.trade-detail-tab {
  display: flex;
  flex-direction: column;
  gap: 20px;
  padding: 20px;
}

/* === 统计卡片网格 === */

.stats-grid {
  display: grid;
  grid-template-columns: repeat(6, 1fr);
  gap: 16px;
}

.stat-card {
  background: var(--panel-bg, #1a2236);
  border-radius: 12px;
  padding: 16px;
  border: 1px solid var(--border, #2a3449);
  text-align: center;
  transition: all 0.3s ease;
}

.stat-card:hover {
  border-color: #2962ff;
  transform: translateY(-2px);
}

.stat-label {
  font-size: 12px;
  color: #94a3b8;
  margin-bottom: 8px;
}

.stat-value {
  font-size: 24px;
  font-weight: 700;
  color: #e0e0e0;
}

.stat-value.positive {
  color: #10b981;
}

.stat-value.negative {
  color: #ff1744;
}

/* === 图表卡片 === */

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

/* === 响应式 === */

@media (max-width: 1400px) {
  .stats-grid {
    grid-template-columns: repeat(3, 1fr);
  }
}

@media (max-width: 768px) {
  .stats-grid {
    grid-template-columns: repeat(2, 1fr);
  }

  .chart-card {
    padding: 16px;
  }

  .chart-container {
    height: 280px;
  }
}
</style>
