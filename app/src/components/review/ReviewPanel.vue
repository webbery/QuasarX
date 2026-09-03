<!-- app/src/components/review/ReviewPanel.vue -->
<!-- 策略复盘面板 - 包含基础指标、归因分析、敏感性分析、交易明细等 Tab -->
<!-- 可嵌入到 StrategyTracker 的历史复盘 Tab 中 -->

<template>
  <div class="review-panel">
    <!-- 回测区间显示 -->
    <div v-if="backtestInfo" class="backtest-info">
      <span class="info-item">📅 {{ backtestInfo.startDate }} ~ {{ backtestInfo.endDate }}</span>
      <span class="info-item">💰 初始资金: ¥{{ formatMoney(backtestInfo.initialCapital) }}</span>
    </div>

    <!-- Tab 切换 -->
    <div class="tabs">
      <button
        v-for="tab in tabs"
        :key="tab.key"
        :class="{ active: activeTab === tab.key }"
        @click="activeTab = tab.key"
      >
        <i :class="tab.icon"></i>
        {{ tab.label }}
      </button>
    </div>

    <!-- Tab 内容区 -->
    <div class="tab-content">
      <!-- 基础指标 -->
      <BasicMetricsTab
        v-if="activeTab === 'metrics'"
        :metrics="metricsData"
        :daily-returns="dailyReturns"
        :nav-curve="navCurve"
        :benchmark-curve="benchmarkCurve"
        :drawdown-curve="drawdownCurve"
      />

      <!-- 归因分析 -->
      <AttributionTab
        v-if="activeTab === 'attribution'"
        :attribution-data="attributionData"
        :period-returns="periodReturns"
        :style-exposure="styleExposure"
      />

      <!-- 敏感性分析 -->
      <SensitivityTab
        v-if="activeTab === 'sensitivity'"
        :param-sensitivity="paramSensitivity"
        :cost-sensitivity="costSensitivity"
        :period-sensitivity="periodSensitivity"
      />

      <!-- 交易明细 -->
      <TradeDetailTab
        v-if="activeTab === 'trades'"
        :trades="tradeRecords"
        :holdings="holdingPeriods"
      />

      <!-- 空状态 -->
      <div v-if="!hasData" class="empty-state">
        <i class="fas fa-chart-line"></i>
        <p>暂无复盘数据</p>
        <span>选择策略并加载回测结果后查看复盘分析</span>
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, onMounted, watch } from 'vue'
import { useHistoryStore, type BacktestResult } from '@/stores/history'
import BasicMetricsTab from './tabs/BasicMetricsTab.vue'
import AttributionTab from './tabs/AttributionTab.vue'
import SensitivityTab from './tabs/SensitivityTab.vue'
import TradeDetailTab from './tabs/TradeDetailTab.vue'

interface Props {
  strategyId?: string
}

const props = defineProps<Props>()

// === Tab 定义 ===
const tabs = [
  { key: 'trades', label: '交易明细', icon: 'fas fa-list' },
  { key: 'metrics', label: '基础指标', icon: 'fas fa-chart-bar' },
  { key: 'attribution', label: '归因分析', icon: 'fas fa-layer-group' },
  { key: 'sensitivity', label: '敏感性分析', icon: 'fas fa-sliders-h' },
]

const activeTab = ref('trades')
const hasData = ref(false)

// === 回测信息 ===
interface BacktestInfo {
  startDate: string
  endDate: string
  initialCapital: number
}

const backtestInfo = ref<BacktestInfo | null>(null)

// === 基础指标数据 ===
const metricsData = ref<Record<string, number>>({})
const dailyReturns = ref<[string, number][]>([])
const navCurve = ref<[string, number][]>([])
const benchmarkCurve = ref<[string, number][]>([])
const drawdownCurve = ref<[string, number][]>([])

// === 归因分析数据 ===
interface AttributionData {
  totalReturn: number
  selectionEffect: number
  allocationEffect: number
  interactionEffect: number
  sectorBreakdown: Array<{ name: string; weight: number; return: number; contribution: number }>
}

const attributionData = ref<AttributionData | null>(null)

interface PeriodReturn {
  period: string
  strategyReturn: number
  benchmarkReturn: number
  excessReturn: number
}

const periodReturns = ref<PeriodReturn[]>([])

interface StyleExposure {
  factor: string
  exposure: number
  contribution: number
}

const styleExposure = ref<StyleExposure[]>([])

// === 敏感性分析数据 ===
interface ParamSensitivity {
  paramName: string
  values: number[]
  returns: number[]
  sharpeRatios: number[]
  maxDrawdowns: number[]
}

const paramSensitivity = ref<ParamSensitivity[]>([])

interface CostSensitivity {
  commissionRate: number
  slippageRate: number
  totalReturn: number
  sharpeRatio: number
  numTrades: number
}

const costSensitivity = ref<CostSensitivity[]>([])

interface PeriodSensitivity {
  period: string
  startDate: string
  endDate: string
  totalReturn: number
  sharpeRatio: number
  maxDrawdown: number
}

const periodSensitivity = ref<PeriodSensitivity[]>([])

// === 交易明细数据 ===
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

const tradeRecords = ref<TradeRecord[]>([])

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

const holdingPeriods = ref<HoldingPeriod[]>([])

// === 公共函数 ===

function formatMoney(amount: number): string {
  return amount.toLocaleString('zh-CN', { minimumFractionDigits: 2, maximumFractionDigits: 2 })
}

function clearData() {
  backtestInfo.value = null
  metricsData.value = {}
  dailyReturns.value = []
  navCurve.value = []
  benchmarkCurve.value = []
  drawdownCurve.value = []
  attributionData.value = null
  periodReturns.value = []
  styleExposure.value = []
  paramSensitivity.value = []
  costSensitivity.value = []
  periodSensitivity.value = []
  tradeRecords.value = []
  holdingPeriods.value = []
  hasData.value = false
}

function formatUnixDate(timestamp: number): string {
  const d = new Date(timestamp * 1000)
  const Y = d.getFullYear() + '-'
  const M = (d.getMonth() + 1 < 10 ? '0' + (d.getMonth() + 1) : d.getMonth() + 1) + '-'
  const D = d.getDate() < 10 ? '0' + d.getDate() : '' + d.getDate()
  return Y + M + D
}

function extractBacktestInfo(result: BacktestResult): BacktestInfo {
  const ts = result.dailyDates && result.dailyDates.length > 0 ? result.dailyDates : null
  const allSignalTs = [
    ...(result.buy || []).map(s => s[1]),
    ...(result.sell || []).map(s => s[1]),
  ]
  const startTs = ts ? ts[0] : (allSignalTs.length > 0 ? Math.min(...allSignalTs) : null)
  const endTs = ts ? ts[ts.length - 1] : (allSignalTs.length > 0 ? Math.max(...allSignalTs) : null)
  return {
    startDate: startTs ? formatUnixDate(startTs) : '-',
    endDate: endTs ? formatUnixDate(endTs) : '-',
    initialCapital: 1000000,
  }
}

function extractDailyReturns(result: BacktestResult): [string, number][] {
  if (!result.dailyReturns || !result.dailyDates) return []
  return result.dailyDates.map((ts, i) => [formatUnixDate(ts), result.dailyReturns![i]])
}

function extractNavCurve(result: BacktestResult): [string, number][] {
  const ts = result.chartData?.performance?.timestamps
  const vals = result.chartData?.performance?.values?.[0]
  if (!ts || !vals) return []
  return ts.map((t, i) => [formatUnixDate(t), vals[i]])
}

function extractBenchmarkCurve(result: BacktestResult): [string, number][] {
  const ts = result.chartData?.performance?.timestamps
  const vals = result.chartData?.performance?.values?.[1]
  if (!ts || !vals) return []
  return ts.map((t, i) => [formatUnixDate(t), vals[i]])
}

function extractDrawdownCurve(result: BacktestResult): [string, number][] {
  const ts = result.chartData?.drawdown?.timestamps
  const vals = result.chartData?.drawdown?.values?.[0]
  if (!ts || !vals) return []
  return ts.map((t, i) => [formatUnixDate(t), -vals[i]])
}

function extractTradesAndHoldings(result: BacktestResult): {
  trades: TradeRecord[]
  holdings: HoldingPeriod[]
} {
  const trades: TradeRecord[] = []
  const holdings: HoldingPeriod[] = []
  type Sig = { dir: 'buy' | 'sell'; sym: string; ts: number; qty: number; price: number }
  const buyQueues = new Map<string, Array<{ ts: number; qty: number; price: number }>>()
  const signals: Sig[] = [
    ...(result.buy || []).map(s => ({ dir: 'buy' as const, sym: s[0], ts: s[1], qty: s[2], price: s[3] })),
    ...(result.sell || []).map(s => ({ dir: 'sell' as const, sym: s[0], ts: s[1], qty: s[2], price: s[3] })),
  ].sort((a, b) => a.ts - b.ts)

  let id = 0
  for (const sig of signals) {
    const trade: TradeRecord = {
      id: `t${id++}`,
      symbol: sig.sym,
      direction: sig.dir,
      price: sig.price,
      quantity: sig.qty,
      amount: sig.price * sig.qty,
      commission: 0,
      timestamp: formatUnixDate(sig.ts),
    }

    if (sig.dir === 'buy') {
      if (!buyQueues.has(sig.sym)) buyQueues.set(sig.sym, [])
      buyQueues.get(sig.sym)!.push({ ts: sig.ts, qty: sig.qty, price: sig.price })
    } else {
      const queue = buyQueues.get(sig.sym)
      if (queue && queue.length > 0) {
        let remainingQty = sig.qty
        let sellPnl = 0
        let matchedQty = 0
        while (remainingQty > 0 && queue.length > 0) {
          const front = queue[0]
          const matchQty = Math.min(front.qty, remainingQty)
          const pnl = (sig.price - front.price) * matchQty
          holdings.push({
            symbol: sig.sym,
            buyDate: formatUnixDate(front.ts),
            sellDate: formatUnixDate(sig.ts),
            buyPrice: front.price,
            sellPrice: sig.price,
            holdingDays: Math.max(1, Math.round((sig.ts - front.ts) / 86400)),
            pnl: parseFloat(pnl.toFixed(2)),
            pnlPercent: parseFloat(((sig.price / front.price - 1) * 100).toFixed(2)),
          })
          sellPnl += pnl
          matchedQty += matchQty
          front.qty -= matchQty
          remainingQty -= matchQty
          if (front.qty === 0) queue.shift()
        }
        if (matchedQty > 0) {
          trade.pnl = parseFloat(sellPnl.toFixed(2))
        }
      }
    }

    trades.push(trade)
  }

  return { trades, holdings }
}

// === 对外暴露方法 ===

async function loadData() {
  clearData()
  if (!props.strategyId) return

  const historyStore = useHistoryStore()
  const version = historyStore.getLatestVersion(props.strategyId)
  if (!version) {
    console.info(`[ReviewPanel] 策略 ${props.strategyId} 暂无版本`)
    return
  }

  const result = await historyStore.loadBacktestResult(version.id)
  if (!result) {
    console.info(`[ReviewPanel] 版本 ${version.id} 暂无回测结果`)
    return
  }

  backtestInfo.value = extractBacktestInfo(result)
  metricsData.value = result.features || {}
  dailyReturns.value = extractDailyReturns(result)
  navCurve.value = extractNavCurve(result)
  benchmarkCurve.value = extractBenchmarkCurve(result)
  drawdownCurve.value = extractDrawdownCurve(result)
  const { trades, holdings } = extractTradesAndHoldings(result)
  tradeRecords.value = trades
  holdingPeriods.value = holdings

  hasData.value = true
}

function reset() {
  clearData()
}

defineExpose({ loadData, reset })

// === 监听策略 ID 变化 ===

watch(() => props.strategyId, (newId) => {
  if (newId) {
    loadData()
  } else {
    clearData()
  }
})

// === 生命周期 ===

onMounted(() => {
  console.info('[ReviewPanel] 组件已挂载')
  // 如果有策略 ID，自动加载数据
  if (props.strategyId) {
    loadData()
  }
})
</script>

<style scoped>
.review-panel {
  height: 100%;
  display: flex;
  flex-direction: column;
  gap: 16px;
  overflow: hidden;
}

/* === 回测信息 === */

.backtest-info {
  display: flex;
  gap: 16px;
  font-size: 13px;
  color: #94a3b8;
  padding: 8px 0;
}

.info-item {
  display: flex;
  align-items: center;
  gap: 6px;
}

/* === Tab 切换 === */

.tabs {
  display: flex;
  gap: 4px;
  border-bottom: 1px solid rgba(74, 158, 255, 0.2);
}

.tabs button {
  padding: 10px 24px;
  background: transparent;
  border: none;
  color: #94a3b8;
  font-size: 14px;
  font-weight: 500;
  cursor: pointer;
  border-bottom: 2px solid transparent;
  transition: all 0.2s;
  display: flex;
  align-items: center;
  gap: 8px;
}

.tabs button:hover {
  color: #e2e8f0;
}

.tabs button.active {
  color: #60a5fa;
  border-bottom-color: #60a5fa;
}

.tabs button i {
  font-size: 14px;
}

/* === 内容区 === */

.tab-content {
  flex: 1;
  overflow: auto;
  display: flex;
  flex-direction: column;
  gap: 16px;
  min-height: 0;
}

/* === 空状态 === */

.empty-state {
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  padding: 60px 20px;
  color: #64748b;
  flex: 1;
}

.empty-state i {
  font-size: 48px;
  margin-bottom: 16px;
  opacity: 0.4;
}

.empty-state p {
  font-size: 18px;
  margin: 0 0 8px;
  color: #94a3b8;
}

.empty-state span {
  font-size: 13px;
}
</style>
