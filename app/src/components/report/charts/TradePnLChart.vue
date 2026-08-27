<!-- app/src/components/report/charts/TradePnLChart.vue -->
<!-- 交易盈亏归因图 - 展示每笔交易的盈亏来源 -->

<template>
  <div class="chart-card full-width">
    <div class="chart-title">
      <div class="title-icon">💰</div>
      <span>Trade P&L Attribution</span>
    </div>

    <div class="summary-bar" v-if="trades.length > 0">
      <div class="summary-item">
        <span class="summary-label">Trades</span>
        <span class="summary-value">{{ trades.length }}</span>
      </div>
      <div class="summary-item">
        <span class="summary-label">Open</span>
        <span class="summary-value">{{ openPositions.length }}</span>
      </div>
      <div class="summary-item">
        <span class="summary-label">Win Rate</span>
        <span class="summary-value" :class="winRate >= 50 ? 'profit' : 'loss'">{{ winRate.toFixed(1) }}%</span>
      </div>
      <div class="summary-item">
        <span class="summary-label">Realized</span>
        <span class="summary-value" :class="totalPnL >= 0 ? 'profit' : 'loss'">{{ formatMoney(totalPnL) }}</span>
      </div>
      <div class="summary-item">
        <span class="summary-label">Unrealized</span>
        <span class="summary-value" :class="openPnL >= 0 ? 'profit' : 'loss'">{{ formatMoney(openPnL) }}</span>
      </div>
      <div class="summary-item">
        <span class="summary-label">Est. Total</span>
        <span class="summary-value" :class="estimatedTotalPnL >= 0 ? 'profit' : 'loss'">{{ formatMoney(estimatedTotalPnL) }}</span>
      </div>
      <div class="summary-item">
        <span class="summary-label">Best</span>
        <span class="summary-value profit">{{ formatMoney(bestTrade) }}</span>
      </div>
      <div class="summary-item">
        <span class="summary-label">Worst</span>
        <span class="summary-value loss">{{ formatMoney(worstTrade) }}</span>
      </div>
    </div>

    <!-- 上图：每笔交易盈亏柱状图 + 累计曲线 + 买卖标注 -->
    <div class="section-label" v-if="trades.length > 0">Per Trade P&L</div>
    <div class="chart-container" ref="tradeChartRef"></div>

    <!-- 下图：按标的聚合 -->
    <div class="section-label" v-if="symbolAgg.length > 0">By Symbol</div>
    <div class="chart-container chart-container--short" ref="symbolChartRef"></div>

    <!-- 标的明细列表：可按列排序，代码旁小图标一键复制 -->
    <div class="section-label" v-if="symbolDetails.length > 0">Symbol Detail · click column to sort</div>
    <div class="symbol-list" v-if="symbolDetails.length > 0">
      <div class="symbol-list-header">
        <span class="cell-code cell-sortable" @click="setSort('symbol')">
          标的代码 <span class="sort-ind">{{ sortKey === 'symbol' ? (sortDir === 'desc' ? '▼' : '▲') : '' }}</span>
        </span>
        <span class="cell-num cell-sortable" @click="setSort('count')">
          笔数 <span class="sort-ind">{{ sortKey === 'count' ? (sortDir === 'desc' ? '▼' : '▲') : '' }}</span>
        </span>
        <span class="cell-num cell-sortable" @click="setSort('winRate')">
          胜率 <span class="sort-ind">{{ sortKey === 'winRate' ? (sortDir === 'desc' ? '▼' : '▲') : '' }}</span>
        </span>
        <span class="cell-num cell-sortable" @click="setSort('realized')">
          已实现 <span class="sort-ind">{{ sortKey === 'realized' ? (sortDir === 'desc' ? '▼' : '▲') : '' }}</span>
        </span>
        <span class="cell-num cell-sortable" @click="setSort('unrealized')">
          未实现 <span class="sort-ind">{{ sortKey === 'unrealized' ? (sortDir === 'desc' ? '▼' : '▲') : '' }}</span>
        </span>
        <span class="cell-num cell-sortable cell-sort-active" @click="setSort('total')">
          总收益 <span class="sort-ind">{{ sortKey === 'total' ? (sortDir === 'desc' ? '▼' : '▲') : '' }}</span>
        </span>
      </div>
      <div v-for="s in sortedSymbolDetails" :key="s.symbol" class="symbol-list-row">
        <span class="cell-code">
          <span class="symbol-code-text" :title="s.symbol">{{ s.symbol }}</span>
          <button
            class="btn-copy"
            :class="{ 'btn-copy--ok': copiedSymbol === s.symbol }"
            @click="copyCode(s.symbol)"
            :title="`复制代码 ${s.symbol}`"
          >
            <i class="copy-glyph">{{ copiedSymbol === s.symbol ? '✓' : '⧉' }}</i>
          </button>
        </span>
        <span class="cell-num">{{ s.count }}</span>
        <span class="cell-num" :class="s.winRate >= 50 ? 'profit' : 'loss'">{{ s.winRate.toFixed(1) }}%</span>
        <span class="cell-num" :class="s.realized >= 0 ? 'profit' : 'loss'">{{ formatMoney(s.realized) }}</span>
        <span class="cell-num" :class="s.unrealized === 0 ? 'cell-zero' : (s.unrealized >= 0 ? 'profit' : 'loss')">
          {{ s.hasOpen ? formatMoney(s.unrealized) : '—' }}
        </span>
        <span class="cell-num" :class="s.total >= 0 ? 'profit' : 'loss'">{{ formatMoney(s.total) }}</span>
      </div>
    </div>

    <div v-if="trades.length === 0 && openPositions.length === 0" class="empty-state">
      <span class="empty-icon">📊</span>
      <span>No completed round-trip trades to display</span>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, computed, watch, onMounted, onUnmounted, nextTick, shallowRef } from 'vue'
import * as echarts from 'echarts'
import { useECharts, createBaseChartOption } from '../composables/useECharts'

interface Props {
  rawBuySignals?: any[]
  rawSellSignals?: any[]
}

const props = withDefaults(defineProps<Props>(), {
  rawBuySignals: () => [],
  rawSellSignals: () => [],
})

interface RoundTrip {
  symbol: string
  entryDate: Date
  exitDate: Date
  entryPrice: number
  exitPrice: number
  quantity: number
  pnl: number
  pnlPct: number
  holdDays: number
}

// --- 独立的双图表 hook ---
const tradeChartRef = ref<HTMLElement | null>(null)
const symbolChartRef = ref<HTMLElement | null>(null)
const tradeChart = shallowRef<echarts.EChartsType | null>(null)
const symbolChart = shallowRef<echarts.EChartsType | null>(null)
let resizeObserver: ResizeObserver | null = null

function initCharts() {
  for (const [el, chartRef] of [[tradeChartRef, tradeChart], [symbolChartRef, symbolChart]] as const) {
    if (!el.value) continue
    if (chartRef.value) chartRef.value.dispose()
    chartRef.value = echarts.init(el.value, 'quasarx-dark')
  }
  resizeObserver = new ResizeObserver(() => {
    tradeChart.value?.resize()
    symbolChart.value?.resize()
  })
  if (tradeChartRef.value) resizeObserver.observe(tradeChartRef.value)
  if (symbolChartRef.value) resizeObserver.observe(symbolChartRef.value)
}

function disposeCharts() {
  resizeObserver?.disconnect()
  resizeObserver = null
  tradeChart.value?.dispose()
  tradeChart.value = null
  symbolChart.value?.dispose()
  symbolChart.value = null
}

onMounted(() => nextTick(initCharts))
onUnmounted(disposeCharts)

// --- 交易配对 ---
const trades = computed<RoundTrip[]>(() => {
  const buys = [...(props.rawBuySignals || [])]
  const sells = [...(props.rawSellSignals || [])]
  if (buys.length === 0 || sells.length === 0) return []

  buys.sort((a, b) => a[1] - b[1])
  sells.sort((a, b) => a[1] - b[1])

  const buysBySymbol: Record<string, any[]> = {}
  for (const b of buys) {
    ;(buysBySymbol[b[0]] ??= []).push(b)
  }
  const sellsBySymbol: Record<string, any[]> = {}
  for (const s of sells) {
    ;(sellsBySymbol[s[0]] ??= []).push(s)
  }

  const result: RoundTrip[] = []
  const allSymbols = new Set([...Object.keys(buysBySymbol), ...Object.keys(sellsBySymbol)])

  for (const symbol of allSymbols) {
    const symBuys = buysBySymbol[symbol] || []
    const symSells = sellsBySymbol[symbol] || []
    let sellIdx = 0

    for (const buy of symBuys) {
      const buyTs = buy[1], buyQty = buy[2], buyPrice = buy[3]
      while (sellIdx < symSells.length) {
        const sell = symSells[sellIdx]
        if (sell[1] < buyTs) { sellIdx++; continue }
        const matchQty = Math.min(buyQty, sell[2])
        const entryDate = new Date(buyTs * 1000)
        const exitDate = new Date(sell[1] * 1000)
        const holdDays = Math.max(1, Math.round((sell[1] - buyTs) / 86400))
        result.push({
          symbol, entryDate, exitDate,
          entryPrice: buyPrice, exitPrice: sell[3],
          quantity: matchQty,
          pnl: (sell[3] - buyPrice) * matchQty,
          pnlPct: buyPrice > 0 ? ((sell[3] / buyPrice) - 1) * 100 : 0,
          holdDays,
        })
        if (sell[2] <= buyQty) sellIdx++
        break
      }
    }
  }

  result.sort((a, b) => a.entryDate.getTime() - b.entryDate.getTime())
  return result
})

// --- 汇总统计 ---
const winRate = computed(() => {
  if (trades.value.length === 0) return 0
  return (trades.value.filter(t => t.pnl > 0).length / trades.value.length) * 100
})
const totalPnL = computed(() => trades.value.reduce((s, t) => s + t.pnl, 0))
const avgPnL = computed(() => trades.value.length > 0 ? totalPnL.value / trades.value.length : 0)
const bestTrade = computed(() => trades.value.length > 0 ? Math.max(...trades.value.map(t => t.pnl)) : 0)
const worstTrade = computed(() => trades.value.length > 0 ? Math.min(...trades.value.map(t => t.pnl)) : 0)

// --- 未平仓持仓检测 ---
interface OpenPosition {
  symbol: string
  quantity: number
  avgEntryPrice: number
  lastPrice: number
  unrealizedPnL: number
}

const openPositions = computed<OpenPosition[]>(() => {
  const buys = props.rawBuySignals || []
  const sells = props.rawSellSignals || []
  if (buys.length === 0) return []

  const boughtBySymbol: Record<string, { totalQty: number; totalCost: number }> = {}
  const soldBySymbol: Record<string, number> = {}
  const lastPriceBySymbol: Record<string, number> = {}

  for (const b of buys) {
    const sym = b[0], qty = b[2], price = b[3]
    const e = boughtBySymbol[sym] ??= { totalQty: 0, totalCost: 0 }
    e.totalQty += qty
    e.totalCost += qty * price
    lastPriceBySymbol[sym] = price
  }
  for (const s of sells) {
    const sym = s[0], qty = s[2], price = s[3]
    soldBySymbol[sym] = (soldBySymbol[sym] || 0) + qty
    lastPriceBySymbol[sym] = price
  }

  const result: OpenPosition[] = []
  for (const [symbol, bought] of Object.entries(boughtBySymbol)) {
    const sold = soldBySymbol[symbol] || 0
    const openQty = bought.totalQty - sold
    if (openQty <= 0) continue
    const avgEntry = bought.totalCost / bought.totalQty
    const lastPrice = lastPriceBySymbol[symbol] || avgEntry
    result.push({
      symbol,
      quantity: openQty,
      avgEntryPrice: avgEntry,
      lastPrice,
      unrealizedPnL: (lastPrice - avgEntry) * openQty,
    })
  }
  return result
})

const openPnL = computed(() => openPositions.value.reduce((s, p) => s + p.unrealizedPnL, 0))
const estimatedTotalPnL = computed(() => totalPnL.value + openPnL.value)

// --- 按标的聚合 ---
const symbolAgg = computed(() => {
  const map: Record<string, { total: number; count: number; wins: number }> = {}
  for (const t of trades.value) {
    const e = map[t.symbol] ??= { total: 0, count: 0, wins: 0 }
    e.total += t.pnl; e.count++
    if (t.pnl > 0) e.wins++
  }
  return Object.entries(map)
    .map(([symbol, v]) => ({ symbol, ...v, winRate: v.count > 0 ? (v.wins / v.count) * 100 : 0 }))
    .sort((a, b) => b.total - a.total)
})

// --- 按标的明细：合并已实现 + 未实现 ---
interface SymbolDetail {
  symbol: string
  count: number
  wins: number
  winRate: number
  realized: number
  unrealized: number
  hasOpen: boolean
  total: number
}

const symbolDetails = computed<SymbolDetail[]>(() => {
  const openMap: Record<string, number> = {}
  for (const p of openPositions.value) openMap[p.symbol] = p.unrealizedPnL

  const seen = new Set<string>()
  const result: SymbolDetail[] = []

  for (const s of symbolAgg.value) {
    seen.add(s.symbol)
    const unrealized = openMap[s.symbol] ?? 0
    result.push({
      symbol: s.symbol,
      count: s.count,
      wins: s.wins,
      winRate: s.winRate,
      realized: s.total,
      unrealized,
      hasOpen: s.symbol in openMap,
      total: s.total + unrealized,
    })
  }

  for (const [symbol, unrealized] of Object.entries(openMap)) {
    if (seen.has(symbol)) continue
    result.push({
      symbol,
      count: 0,
      wins: 0,
      winRate: 0,
      realized: 0,
      unrealized,
      hasOpen: true,
      total: unrealized,
    })
  }

  return result
})

// --- 列表排序状态 ---
type SortKey = 'symbol' | 'count' | 'winRate' | 'realized' | 'unrealized' | 'total'
const sortKey = ref<SortKey>('total')
const sortDir = ref<'asc' | 'desc'>('desc')

function setSort(key: SortKey) {
  if (sortKey.value === key) {
    sortDir.value = sortDir.value === 'desc' ? 'asc' : 'desc'
  } else {
    sortKey.value = key
    // 数字列默认降序（看大头）；文本列默认升序（字母序）
    sortDir.value = key === 'symbol' ? 'asc' : 'desc'
  }
}

const sortedSymbolDetails = computed<SymbolDetail[]>(() => {
  const k = sortKey.value
  const dir = sortDir.value === 'desc' ? -1 : 1
  return [...symbolDetails.value].sort((a, b) => {
    const av = (a as any)[k]
    const bv = (b as any)[k]
    if (typeof av === 'string' && typeof bv === 'string') {
      return av.localeCompare(bv) * dir
    }
    return (Number(av) - Number(bv)) * dir
  })
})

// --- 复制标的代码到剪贴板 ---
const copiedSymbol = ref<string | null>(null)
let copyTimer: ReturnType<typeof setTimeout> | null = null

function copyCode(symbol: string) {
  if (copyTimer) clearTimeout(copyTimer)
  navigator.clipboard.writeText(symbol).then(() => {
    copiedSymbol.value = symbol
    copyTimer = setTimeout(() => { copiedSymbol.value = null }, 900)
  }).catch(() => {
    copiedSymbol.value = symbol + '×'
    copyTimer = setTimeout(() => { copiedSymbol.value = null }, 1200)
  })
}

onUnmounted(() => { if (copyTimer) clearTimeout(copyTimer) })

// --- 工具函数 ---
function formatMoney(val: number): string {
  const abs = Math.abs(val)
  if (abs >= 1e6) return (val / 1e6).toFixed(2) + 'M'
  if (abs >= 1e3) return (val / 1e3).toFixed(1) + 'K'
  return val.toFixed(0)
}
function fmtDate(d: Date): string {
  return `${d.getFullYear()}-${String(d.getMonth() + 1).padStart(2, '0')}-${String(d.getDate()).padStart(2, '0')}`
}
function shortSymbol(s: string): string {
  return s.split('.').pop() || s
}

// 颜色映射：为不同标的分配不同颜色
const SYMBOL_COLORS = [
  '#2962ff', '#ff9800', '#00c853', '#ff6d00', '#aa00ff',
  '#00bcd4', '#e91e63', '#8bc34a', '#ff5722', '#3f51b5',
]
function symbolColor(sym: string, idx: number): string {
  return SYMBOL_COLORS[idx % SYMBOL_COLORS.length]
}

// --- 渲染上图：逐笔 P&L ---
function renderTradeChart() {
  if (!tradeChart.value || trades.value.length === 0) return

  const n = trades.value.length
  const hasOpen = openPositions.value.length > 0
  const labels = trades.value.map((t, i) => `${i + 1}`)
  if (hasOpen) labels.push('Open')

  const pnls = trades.value.map(t => Number(t.pnl.toFixed(2)))
  const cumPnls: number[] = []
  let cum = 0
  for (const p of pnls) { cum += p; cumPnls.push(Number(cum.toFixed(2))) }

  // Est. Total P&L 线：从最后一笔 realized cum 跳到 open 位置的估算总值
  // 只在有未平仓时显示，形成可见的桥接线
  const estTotalLine: (number | null)[] = hasOpen
    ? [...Array(n - 1).fill(null), cumPnls[n - 1], Number(estimatedTotalPnL.value.toFixed(2))]
    : []

  // 为每根柱子按标的着色
  const symIndexMap: Record<string, number> = {}
  let colorIdx = 0
  for (const t of trades.value) {
    if (!(t.symbol in symIndexMap)) symIndexMap[t.symbol] = colorIdx++
  }

  const barData = trades.value.map((t, i) => {
    const v = pnls[i]
    const baseColor = symbolColor(t.symbol, symIndexMap[t.symbol])
    const isProfit = v >= 0
    return {
      value: v,
      itemStyle: {
        color: new echarts.graphic.LinearGradient(0, 0, 0, 1, [
          { offset: 0, color: isProfit ? baseColor : adjustAlpha(baseColor, 0.3) },
          { offset: 1, color: isProfit ? adjustAlpha(baseColor, 0.3) : baseColor },
        ]),
        borderRadius: isProfit ? [4, 4, 0, 0] : [0, 0, 4, 4],
      },
    }
  })

  // 买入/卖出标记线（在柱状图上用 markLine 标注持仓天数）
  const option = createBaseChartOption({
    legend: {
      data: ['Trade P&L', 'Cumulative P&L', 'Est. Total P&L'],
      textStyle: { color: '#a0aec0' },
      top: 6,
    },
    xAxis: {
      type: 'category',
      data: labels,
      name: 'Trade #',
      nameTextStyle: { color: '#a0aec0', fontSize: 11 },
      axisLabel: {
        color: '#a0aec0',
        fontSize: 10,
        interval: Math.max(0, Math.floor(n / 30) - 1),
        formatter: (value: string) => value === 'Open' ? '{open|Open}' : value,
        rich: {
          open: { color: '#ff9800', fontWeight: 'bold', fontSize: 11 },
        },
      },
      axisLine: { lineStyle: { color: '#2a3449' } },
    },
    yAxis: [
      {
        type: 'value',
        name: 'P&L',
        nameTextStyle: { color: '#a0aec0' },
        axisLabel: { color: '#a0aec0', formatter: (v: number) => formatMoney(v) },
        splitLine: { lineStyle: { color: '#2a3449', type: 'dashed' } },
      },
      {
        type: 'value',
        name: 'Cumulative',
        nameTextStyle: { color: '#a0aec0' },
        axisLabel: { color: '#a0aec0', formatter: (v: number) => formatMoney(v) },
        splitLine: { show: false },
      },
    ],
    series: [
      {
        name: 'Trade P&L',
        type: 'bar',
        data: barData,
        barMaxWidth: 40,
      },
      {
        name: 'Cumulative P&L',
        type: 'line',
        yAxisIndex: 1,
        data: cumPnls,
        smooth: true,
        symbol: 'circle',
        symbolSize: 5,
        lineStyle: { color: '#2962ff', width: 2.5 },
        itemStyle: { color: '#2962ff' },
        areaStyle: {
          color: new echarts.graphic.LinearGradient(0, 0, 0, 1, [
            { offset: 0, color: 'rgba(41, 98, 255, 0.12)' },
            { offset: 1, color: 'rgba(41, 98, 255, 0)' },
          ]),
        },
      },
      {
        name: 'Est. Total P&L',
        type: 'line',
        yAxisIndex: 1,
        data: estTotalLine,
        connectNulls: true,
        symbol: 'diamond',
        symbolSize: 8,
        lineStyle: { color: '#ff9800', width: 2.5, type: 'dashed' },
        itemStyle: { color: '#ff9800' },
      },
    ],
    tooltip: {
      trigger: 'axis',
      backgroundColor: 'rgba(26, 34, 54, 0.95)',
      borderColor: '#2a3449',
      textStyle: { color: '#e0e0e0', fontSize: 12 },
      formatter: (params: any) => {
        const idx = params[0]?.dataIndex
        if (idx === undefined || idx >= trades.value.length) return ''
        const t = trades.value[idx]
        const c = symbolColor(t.symbol, symIndexMap[t.symbol])
        const pnlColor = t.pnl >= 0 ? '#00c853' : '#ff1744'
        const isLast = idx === trades.value.length - 1
        let html = `
          <div style="font-weight:600;margin-bottom:4px;color:${c}">${t.symbol}</div>
          <div>Entry: ${fmtDate(t.entryDate)} @ ${t.entryPrice.toFixed(2)}</div>
          <div>Exit: ${fmtDate(t.exitDate)} @ ${t.exitPrice.toFixed(2)}</div>
          <div>Qty: ${t.quantity.toLocaleString()} shares · Hold: ${t.holdDays}d</div>
          <div style="margin-top:4px;color:${pnlColor};font-weight:600">
            P&L: ${t.pnl >= 0 ? '+' : ''}${t.pnl.toFixed(2)} (${t.pnlPct >= 0 ? '+' : ''}${t.pnlPct.toFixed(2)}%)
          </div>
          <div style="color:#2962ff">Realized Cum: ${cumPnls[idx] >= 0 ? '+' : ''}${cumPnls[idx].toFixed(2)}</div>
        `
        if (isLast && openPositions.value.length > 0) {
          html += `<div style="margin-top:6px;border-top:1px solid #2a3449;padding-top:4px">`
          html += `<div style="color:#ff9800;font-weight:600">Est. Total: ${estimatedTotalPnL.value >= 0 ? '+' : ''}${estimatedTotalPnL.value.toFixed(2)}</div>`
          html += `<div style="color:#a0aec0;font-size:11px">Unrealized: ${openPnL.value >= 0 ? '+' : ''}${openPnL.value.toFixed(2)} (${openPositions.value.length} open pos)</div>`
          for (const pos of openPositions.value) {
            const posColor = pos.unrealizedPnL >= 0 ? '#00c853' : '#ff1744'
            html += `<div style="color:${posColor};font-size:11px">${pos.symbol}: ${pos.quantity} shares @ ${pos.avgEntryPrice.toFixed(2)} → ${pos.lastPrice.toFixed(2)}</div>`
          }
          html += `</div>`
        }
        return html
      },
    },
    grid: { left: '3%', right: '4%', bottom: '10%', top: '14%', containLabel: true },
  })

  tradeChart.value.setOption(option, true)
}

function adjustAlpha(hex: string, alpha: number): string {
  const r = parseInt(hex.slice(1, 3), 16)
  const g = parseInt(hex.slice(3, 5), 16)
  const b = parseInt(hex.slice(5, 7), 16)
  return `rgba(${r},${g},${b},${alpha})`
}

// --- 渲染下图：按标的聚合 ---
function renderSymbolChart() {
  if (!symbolChart.value || symbolAgg.value.length === 0) return

  const labels = symbolAgg.value.map(s => shortSymbol(s.symbol))
  const pnls = symbolAgg.value.map(s => Number(s.total.toFixed(2)))
  const counts = symbolAgg.value.map(s => s.count)
  const winRates = symbolAgg.value.map(s => Number(s.winRate.toFixed(1)))

  const symIndexMap: Record<string, number> = {}
  symbolAgg.value.forEach((s, i) => { symIndexMap[s.symbol] = i })

  const barData = symbolAgg.value.map((s, i) => ({
    value: pnls[i],
    itemStyle: {
      color: new echarts.graphic.LinearGradient(0, 0, 0, 1, [
        { offset: 0, color: pnls[i] >= 0 ? symbolColor(s.symbol, i) : adjustAlpha(symbolColor(s.symbol, i), 0.3) },
        { offset: 1, color: pnls[i] >= 0 ? adjustAlpha(symbolColor(s.symbol, i), 0.3) : symbolColor(s.symbol, i) },
      ]),
      borderRadius: pnls[i] >= 0 ? [4, 4, 0, 0] : [0, 0, 4, 4],
    },
  }))

  const option = createBaseChartOption({
    legend: {
      data: ['Total P&L', 'Trades', 'Win Rate'],
      textStyle: { color: '#a0aec0' },
      top: 6,
    },
    xAxis: {
      type: 'category',
      data: labels,
      axisLabel: { color: '#a0aec0', fontSize: 11 },
      axisLine: { lineStyle: { color: '#2a3449' } },
    },
    yAxis: [
      {
        type: 'value',
        name: 'P&L / Count',
        nameTextStyle: { color: '#a0aec0' },
        axisLabel: { color: '#a0aec0', formatter: (v: number) => formatMoney(v) },
        splitLine: { lineStyle: { color: '#2a3449', type: 'dashed' } },
      },
      {
        type: 'value',
        name: 'Win Rate %',
        nameTextStyle: { color: '#a0aec0' },
        min: 0, max: 100,
        axisLabel: { color: '#a0aec0', formatter: '{value}%' },
        splitLine: { show: false },
      },
    ],
    series: [
      {
        name: 'Total P&L',
        type: 'bar',
        data: barData,
        barMaxWidth: 50,
      },
      {
        name: 'Trades',
        type: 'bar',
        data: counts,
        barMaxWidth: 28,
        itemStyle: { color: 'rgba(41, 98, 255, 0.4)', borderRadius: [3, 3, 0, 0] },
      },
      {
        name: 'Win Rate',
        type: 'line',
        yAxisIndex: 1,
        data: winRates,
        symbol: 'circle',
        symbolSize: 8,
        lineStyle: { color: '#ff9800', width: 2 },
        itemStyle: { color: '#ff9800' },
      },
    ],
    tooltip: {
      trigger: 'axis',
      backgroundColor: 'rgba(26, 34, 54, 0.95)',
      borderColor: '#2a3449',
      textStyle: { color: '#e0e0e0', fontSize: 12 },
      formatter: (params: any) => {
        const idx = params[0]?.dataIndex
        if (idx === undefined || idx >= symbolAgg.value.length) return ''
        const s = symbolAgg.value[idx]
        const pnlColor = s.total >= 0 ? '#00c853' : '#ff1744'
        return `
          <div style="font-weight:600;margin-bottom:4px">${s.symbol}</div>
          <div>Trades: ${s.count} · Win Rate: ${s.winRate.toFixed(1)}%</div>
          <div style="margin-top:4px;color:${pnlColor};font-weight:600">
            Total P&L: ${s.total >= 0 ? '+' : ''}${s.total.toFixed(2)}
          </div>
        `
      },
    },
    grid: { left: '3%', right: '4%', bottom: '5%', top: '14%', containLabel: true },
  })

  symbolChart.value.setOption(option, true)
}

// --- 响应数据变化 ---
function renderAll() {
  nextTick(() => {
    renderTradeChart()
    renderSymbolChart()
  })
}

watch(() => [props.rawBuySignals, props.rawSellSignals], renderAll, { deep: true })
onMounted(() => renderAll())
</script>

<style scoped>
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
  display: flex;
  align-items: center;
  gap: 8px;
  margin-bottom: 12px;
  font-size: 15px;
  font-weight: 600;
  color: #e0e0e0;
}

.title-icon { font-size: 18px; }

.summary-bar {
  display: flex;
  gap: 16px;
  padding: 10px 16px;
  background: rgba(41, 98, 255, 0.05);
  border: 1px solid var(--border, #2a3449);
  border-radius: 8px;
  margin-bottom: 16px;
  flex-wrap: wrap;
}

.summary-item {
  display: flex;
  flex-direction: column;
  align-items: center;
  min-width: 70px;
}

.summary-label {
  font-size: 10px;
  color: #a0aec0;
  text-transform: uppercase;
  letter-spacing: 0.5px;
}

.summary-value {
  font-size: 14px;
  font-weight: 600;
  color: #e0e0e0;
  margin-top: 2px;
}

.summary-value.profit { color: #00c853; }
.summary-value.loss { color: #ff1744; }

.section-label {
  font-size: 12px;
  color: #a0aec0;
  margin-bottom: 4px;
  margin-top: 8px;
  font-weight: 500;
}

.chart-container {
  width: 100%;
  height: 360px;
  min-height: 280px;
}

.chart-container--short {
  height: 260px;
  min-height: 200px;
}

.empty-state {
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  height: 200px;
  color: #a0aec0;
  gap: 8px;
}

.empty-icon { font-size: 36px; opacity: 0.5; }

/* === 标的明细列表 === */
.symbol-list {
  display: flex;
  flex-direction: column;
  border: 1px solid var(--border, #2a3449);
  border-radius: 8px;
  overflow: hidden;
  margin-top: 4px;
  font-size: 12px;
}

.symbol-list-header,
.symbol-list-row {
  display: grid;
  grid-template-columns: minmax(120px, 1.6fr) 60px 70px 90px 90px 100px;
  align-items: center;
  padding: 8px 12px;
  gap: 8px;
}

.symbol-list-header {
  background: rgba(41, 98, 255, 0.08);
  border-bottom: 1px solid var(--border, #2a3449);
  color: #a0aec0;
  font-weight: 600;
  text-transform: uppercase;
  letter-spacing: 0.4px;
  font-size: 10px;
}

.symbol-list-row {
  border-bottom: 1px solid rgba(42, 52, 73, 0.4);
  color: #e0e0e0;
  transition: background 0.15s ease;
}
.symbol-list-row:last-child { border-bottom: none; }
.symbol-list-row:hover { background: rgba(41, 98, 255, 0.06); }

.cell-code {
  display: flex;
  align-items: center;
  gap: 6px;
  min-width: 0;
}
.symbol-code-text {
  font-family: 'SF Mono', Menlo, Consolas, monospace;
  font-size: 12px;
  color: #e0e0e0;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
  flex: 1;
}
.cell-num {
  font-variant-numeric: tabular-nums;
  text-align: right;
  font-family: 'SF Mono', Menlo, Consolas, monospace;
}
.cell-zero { color: #6b7280; }

.cell-sortable {
  cursor: pointer;
  user-select: none;
  transition: color 0.15s ease;
}
.cell-sortable:hover { color: #e0e0e0; }
.cell-sort-active { color: #2962ff; }
.sort-ind { font-size: 9px; margin-left: 2px; opacity: 0.8; }

.btn-copy {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  width: 22px;
  height: 22px;
  padding: 0;
  border: 1px solid var(--border, #2a3449);
  border-radius: 4px;
  background: rgba(41, 98, 255, 0.08);
  color: #a0aec0;
  cursor: pointer;
  transition: all 0.15s ease;
  flex-shrink: 0;
}
.btn-copy:hover {
  background: rgba(41, 98, 255, 0.22);
  color: #2962ff;
  border-color: #2962ff;
}
.btn-copy:active { transform: scale(0.92); }
.btn-copy--ok {
  background: rgba(0, 200, 83, 0.18);
  border-color: #00c853;
  color: #00c853;
}
.copy-glyph {
  font-style: normal;
  font-size: 12px;
  line-height: 1;
  font-weight: 700;
}
</style>
