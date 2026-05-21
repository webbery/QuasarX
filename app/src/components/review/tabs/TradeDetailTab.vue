<!-- app/src/components/review/tabs/TradeDetailTab.vue -->
<!-- 交易明细 Tab - 交易记录表格、持仓周期分析、盈亏分布 -->

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

    <!-- 持仓周期分析 -->
    <div v-if="holdings.length > 0" class="chart-card">
      <div class="chart-title">
        <div class="title-icon">⏱️</div>
        <span>持仓周期分析</span>
      </div>
      <div class="chart-container" ref="holdingPeriodRef"></div>
    </div>

    <!-- 交易记录表格 -->
    <div class="chart-card">
      <div class="chart-title">
        <div class="title-icon">📋</div>
        <span>交易记录明细</span>
        <div class="table-actions">
          <button class="btn-export" @click="exportCSV" :disabled="trades.length === 0">
            <i class="fas fa-download"></i> 导出CSV
          </button>
        </div>
      </div>
      <div class="trade-table">
        <table class="data-table">
          <thead>
            <tr>
              <th>时间</th>
              <th>标的</th>
              <th>方向</th>
              <th>价格</th>
              <th>数量</th>
              <th>金额</th>
              <th>佣金</th>
              <th>盈亏</th>
            </tr>
          </thead>
          <tbody>
            <tr v-for="trade in sortedTrades" :key="trade.id">
              <td>{{ trade.timestamp }}</td>
              <td>{{ trade.symbol }}</td>
              <td>
                <span :class="trade.direction === 'buy' ? 'badge-buy' : 'badge-sell'">
                  {{ trade.direction === 'buy' ? '买入' : '卖出' }}
                </span>
              </td>
              <td>¥{{ trade.price.toFixed(2) }}</td>
              <td>{{ trade.quantity }}</td>
              <td>¥{{ trade.amount.toFixed(2) }}</td>
              <td>¥{{ trade.commission.toFixed(2) }}</td>
              <td :class="trade.pnl !== undefined ? getReturnClass(trade.pnl) : ''">
                {{ trade.pnl !== undefined ? `¥${trade.pnl.toFixed(2)}` : '-' }}
              </td>
            </tr>
          </tbody>
        </table>
      </div>
    </div>

    <!-- 持仓周期表格 -->
    <div v-if="holdings.length > 0" class="chart-card">
      <div class="chart-title">
        <div class="title-icon">📅</div>
        <span>持仓周期明细</span>
      </div>
      <div class="holding-table">
        <table class="data-table">
          <thead>
            <tr>
              <th>标的</th>
              <th>买入日期</th>
              <th>卖出日期</th>
              <th>买入价</th>
              <th>卖出价</th>
              <th>持仓天数</th>
              <th>盈亏</th>
              <th>收益率</th>
            </tr>
          </thead>
          <tbody>
            <tr v-for="h in sortedHoldings" :key="h.symbol + h.buyDate">
              <td>{{ h.symbol }}</td>
              <td>{{ h.buyDate }}</td>
              <td>{{ h.sellDate }}</td>
              <td>¥{{ h.buyPrice.toFixed(2) }}</td>
              <td>¥{{ h.sellPrice.toFixed(2) }}</td>
              <td>{{ h.holdingDays }} 天</td>
              <td :class="getReturnClass(h.pnl)">
                ¥{{ h.pnl.toFixed(2) }}
              </td>
              <td :class="getReturnClass(h.pnlPercent)">
                {{ h.pnlPercent >= 0 ? '+' : '' }}{{ h.pnlPercent.toFixed(2) }}%
              </td>
            </tr>
          </tbody>
        </table>
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, computed, watch, onMounted, onUnmounted } from 'vue'
import * as echarts from 'echarts'

interface TradeRecord {
  id: string
  symbol: string
  direction: 'buy' | 'sell'
  price: number
  quantity: number
  amount: number
  commission: number
  timestamp: string
  pnl?: number
}

interface HoldingPeriod {
  symbol: string
  buyDate: string
  sellDate: string
  buyPrice: number
  sellPrice: number
  holdingDays: number
  pnl: number
  pnlPercent: number
}

interface Props {
  trades: TradeRecord[]
  holdings: HoldingPeriod[]
}

const props = defineProps<Props>()

// === 图表引用 ===
const pnlDistRef = ref<HTMLElement | null>(null)
const cumPnlRef = ref<HTMLElement | null>(null)
const holdingPeriodRef = ref<HTMLElement | null>(null)

let pnlDistChart: echarts.EChartsType | null = null
let cumPnlChart: echarts.EChartsType | null = null
let holdingPeriodChart: echarts.EChartsType | null = null

// === 计算属性 ===

const sortedTrades = computed(() => {
  return [...props.trades].sort((a, b) => a.timestamp.localeCompare(b.timestamp))
})

const sortedHoldings = computed(() => {
  return [...props.holdings].sort((a, b) => a.buyDate.localeCompare(b.buyDate))
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

// === 辅助函数 ===

function getReturnClass(value: number): string {
  return value >= 0 ? 'return-positive' : 'return-negative'
}

// === 盈亏分布图表 ===

function initPnlDistChart() {
  if (!pnlDistRef.value) return
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
        const val = params[0].value[1]
        return `${params[0].value[0]}<br/>累计盈亏: <span style="color: ${val >= 0 ? '#10b981' : '#ff1744'}; font-weight: bold;">¥${val.toFixed(2)}</span>`
      },
    },
    grid: { left: '3%', right: '4%', bottom: '12%', containLabel: true },
    xAxis: {
      type: 'category',
      data: cumPnlData.map(d => d[0]),
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
      data: cumPnlData.map(d => d[1]),
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

// === 持仓周期图表 ===

function initHoldingPeriodChart() {
  if (!holdingPeriodRef.value) return
  holdingPeriodChart = echarts.init(holdingPeriodRef.value, 'quasarx-dark')
}

function updateHoldingPeriodChart() {
  if (!holdingPeriodChart || props.holdings.length === 0) return

  const holdings = sortedHoldings.value
  const labels = holdings.map((h, i) => `#${i + 1}`)
  const days = holdings.map(h => h.holdingDays)
  const pnlPercents = holdings.map(h => h.pnlPercent)

  const option = {
    tooltip: {
      trigger: 'axis',
      backgroundColor: 'rgba(26, 34, 54, 0.9)',
      borderColor: '#2a3449',
      textStyle: { color: '#e0e0e0' },
    },
    legend: {
      data: ['持仓天数', '收益率'],
      textStyle: { color: '#a0aec0' },
      top: 10,
    },
    grid: { left: '3%', right: '4%', bottom: '15%', containLabel: true },
    xAxis: {
      type: 'category',
      data: labels,
      axisLine: { lineStyle: { color: '#6E7079' } },
      axisLabel: { color: '#a0aec0' },
      splitLine: { show: false },
    },
    yAxis: [
      {
        type: 'value',
        name: '持仓天数',
        axisLine: { lineStyle: { color: '#6E7079' } },
        axisLabel: { color: '#a0aec0', formatter: '{value} 天' },
        splitLine: { lineStyle: { color: '#2a3449', type: 'dashed' } },
      },
      {
        type: 'value',
        name: '收益率',
        axisLine: { lineStyle: { color: '#6E7079' } },
        axisLabel: { color: '#a0aec0', formatter: '{value}%' },
        splitLine: { show: false },
      },
    ],
    series: [
      {
        name: '持仓天数',
        type: 'bar',
        data: days.map(v => ({
          value: v,
          itemStyle: { color: '#2962ff' },
        })),
        yAxisIndex: 0,
      },
      {
        name: '收益率',
        type: 'line',
        data: pnlPercents.map(v => ({
          value: v,
          itemStyle: { color: v >= 0 ? '#10b981' : '#ff1744' },
        })),
        yAxisIndex: 1,
        lineStyle: { width: 3 },
        symbol: 'circle',
        symbolSize: 6,
      },
    ],
  }

  holdingPeriodChart.setOption(option, true)
}

// === 导出 CSV ===

function exportCSV() {
  if (props.trades.length === 0) return

  const lines = ['时间,标的,方向,价格,数量,金额,佣金,盈亏']
  sortedTrades.value.forEach(t => {
    lines.push(`${t.timestamp},${t.symbol},${t.direction === 'buy' ? '买入' : '卖出'},${t.price},${t.quantity},${t.amount},${t.commission},${t.pnl !== undefined ? t.pnl : ''}`)
  })

  const csvContent = lines.join('\n')
  const blob = new Blob(['\uFEFF' + csvContent], { type: 'text/csv;charset=utf-8;' })
  const url = URL.createObjectURL(blob)

  const a = document.createElement('a')
  a.href = url
  a.download = `trades_${new Date().toISOString().split('T')[0]}.csv`
  a.click()
  URL.revokeObjectURL(url)
}

// === 响应式更新 ===

watch(() => props.trades, () => {
  updatePnlDistChart()
  updateCumPnlChart()
}, { deep: true })

watch(() => props.holdings, updateHoldingPeriodChart, { deep: true })

// === 生命周期 ===

onMounted(() => {
  initPnlDistChart()
  initCumPnlChart()
  initHoldingPeriodChart()

  updatePnlDistChart()
  updateCumPnlChart()
  updateHoldingPeriodChart()

  window.addEventListener('resize', onResize)
})

onUnmounted(() => {
  pnlDistChart?.dispose()
  cumPnlChart?.dispose()
  holdingPeriodChart?.dispose()
  window.removeEventListener('resize', onResize)
})

function onResize() {
  pnlDistChart?.resize()
  cumPnlChart?.resize()
  holdingPeriodChart?.resize()
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

.table-actions {
  margin-left: auto;
}

.btn-export {
  padding: 6px 16px;
  background: linear-gradient(90deg, #059669, #047857);
  color: white;
  border: none;
  border-radius: 6px;
  font-weight: 600;
  cursor: pointer;
  transition: all 0.2s;
  display: flex;
  align-items: center;
  gap: 6px;
  font-size: 13px;
}

.btn-export:hover:not(:disabled) {
  background: linear-gradient(90deg, #047857, #065f46);
}

.btn-export:disabled {
  opacity: 0.4;
  cursor: not-allowed;
}

/* === 表格 === */

.trade-table,
.holding-table {
  overflow-x: auto;
  max-height: 500px;
  overflow-y: auto;
}

.data-table {
  width: 100%;
  border-collapse: collapse;
  font-size: 13px;
}

.data-table thead tr {
  border-bottom: 2px solid var(--border, #2a3449);
  position: sticky;
  top: 0;
  background: var(--panel-bg, #1a2236);
  z-index: 10;
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

/* === 买卖标签 === */

.badge-buy {
  display: inline-block;
  padding: 2px 8px;
  background: rgba(16, 185, 129, 0.2);
  color: #10b981;
  border-radius: 4px;
  font-size: 12px;
  font-weight: 600;
}

.badge-sell {
  display: inline-block;
  padding: 2px 8px;
  background: rgba(255, 23, 68, 0.2);
  color: #ff1744;
  border-radius: 4px;
  font-size: 12px;
  font-weight: 600;
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

  .data-table {
    font-size: 12px;
  }

  .data-table th,
  .data-table td {
    padding: 8px 10px;
  }
}
</style>
