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
import { ref, computed, onMounted, watch } from 'vue'
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
  { key: 'metrics', label: '基础指标', icon: 'fas fa-chart-bar' },
  { key: 'attribution', label: '归因分析', icon: 'fas fa-layer-group' },
  { key: 'sensitivity', label: '敏感性分析', icon: 'fas fa-sliders-h' },
  { key: 'trades', label: '交易明细', icon: 'fas fa-list' },
]

const activeTab = ref('metrics')
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

function generateDateRange(startDate: string, endDate: string): string[] {
  const dates: string[] = []
  let current = new Date(startDate)
  const end = new Date(endDate)

  while (current <= end) {
    const day = current.getDay()
    if (day !== 0 && day !== 6) {
      dates.push(current.toISOString().split('T')[0])
    }
    current.setDate(current.getDate() + 1)
  }

  return dates
}

function generateMockTrades(count: number): TradeRecord[] {
  const trades: TradeRecord[] = []
  const symbols = ['600000.SH', '000001.SZ', '600519.SH', '000858.SZ', '601318.SH']
  let date = new Date('2023-01-01')

  for (let i = 0; i < count; i++) {
    const symbol = symbols[i % symbols.length]
    const direction = i % 2 === 0 ? 'buy' : 'sell'
    const price = 10 + Math.random() * 50
    const quantity = Math.floor(100 + Math.random() * 900)
    const amount = price * quantity
    const commission = amount * 0.0003

    trades.push({
      id: `trade-${i}`,
      symbol,
      direction,
      price: parseFloat(price.toFixed(2)),
      quantity,
      amount: parseFloat(amount.toFixed(2)),
      commission: parseFloat(commission.toFixed(2)),
      timestamp: date.toISOString().split('T')[0],
      pnl: direction === 'sell' ? (Math.random() - 0.4) * amount * 0.1 : undefined,
    })

    date.setDate(date.getDate() + Math.floor(Math.random() * 5) + 1)
  }

  return trades
}

function generateMockHoldingPeriods(count: number): HoldingPeriod[] {
  const periods: HoldingPeriod[] = []
  const symbols = ['600000.SH', '000001.SZ', '600519.SH', '000858.SZ', '601318.SH']
  let buyDate = new Date('2023-01-01')

  for (let i = 0; i < count; i++) {
    const symbol = symbols[i % symbols.length]
    const buyPrice = 10 + Math.random() * 50
    const sellPrice = buyPrice * (1 + (Math.random() - 0.4) * 0.2)
    const holdingDays = Math.floor(5 + Math.random() * 60)
    const pnl = (sellPrice - buyPrice) * 100
    const pnlPercent = (sellPrice - buyPrice) / buyPrice * 100

    const sellDate = new Date(buyDate)
    sellDate.setDate(sellDate.getDate() + holdingDays)

    periods.push({
      symbol,
      buyDate: buyDate.toISOString().split('T')[0],
      sellDate: sellDate.toISOString().split('T')[0],
      buyPrice: parseFloat(buyPrice.toFixed(2)),
      sellPrice: parseFloat(sellPrice.toFixed(2)),
      holdingDays,
      pnl: parseFloat(pnl.toFixed(2)),
      pnlPercent: parseFloat(pnlPercent.toFixed(2)),
    })

    buyDate = sellDate
  }

  return periods
}

function loadMockData() {
  backtestInfo.value = {
    startDate: '2023-01-01',
    endDate: '2024-12-31',
    initialCapital: 1000000,
  }

  metricsData.value = {
    total_return: 0.3542,
    annual_return: 0.1623,
    max_drawdown: 0.1245,
    volatility: 0.1834,
    sharp: 0.82,
    calmar_ratio: 1.30,
    win_rate: 0.58,
    num_trades: 156,
    VAR: 0.0234,
    ES: 0.0312,
  }

  const dates = generateDateRange('2023-01-01', '2024-12-31')
  dailyReturns.value = dates.map(d => [d, (Math.random() - 0.48) * 0.03])

  let nav = 1.0
  navCurve.value = dates.map(d => {
    const ret = (Math.random() - 0.48) * 0.03
    nav *= (1 + ret)
    return [d, nav]
  })

  let bench = 1.0
  benchmarkCurve.value = dates.map(d => {
    const ret = (Math.random() - 0.50) * 0.025
    bench *= (1 + ret)
    return [d, bench]
  })

  let peak = 1.0
  drawdownCurve.value = navCurve.value.map(([d, navVal]) => {
    if (navVal > peak) peak = navVal
    const dd = (peak - navVal) / peak
    return [d, -dd]
  })

  attributionData.value = {
    totalReturn: 0.3542,
    selectionEffect: 0.12,
    allocationEffect: 0.08,
    interactionEffect: 0.03,
    sectorBreakdown: [
      { name: '金融', weight: 0.25, return: 0.18, contribution: 0.045 },
      { name: '科技', weight: 0.30, return: 0.25, contribution: 0.075 },
      { name: '消费', weight: 0.20, return: 0.12, contribution: 0.024 },
      { name: '医药', weight: 0.15, return: 0.08, contribution: 0.012 },
      { name: '其他', weight: 0.10, return: 0.15, contribution: 0.015 },
    ],
  }

  periodReturns.value = [
    { period: '2023 Q1', strategyReturn: 0.085, benchmarkReturn: 0.062, excessReturn: 0.023 },
    { period: '2023 Q2', strategyReturn: 0.042, benchmarkReturn: 0.038, excessReturn: 0.004 },
    { period: '2023 Q3', strategyReturn: -0.035, benchmarkReturn: -0.042, excessReturn: 0.007 },
    { period: '2023 Q4', strategyReturn: 0.068, benchmarkReturn: 0.055, excessReturn: 0.013 },
    { period: '2024 Q1', strategyReturn: 0.092, benchmarkReturn: 0.071, excessReturn: 0.021 },
    { period: '2024 Q2', strategyReturn: 0.055, benchmarkReturn: 0.048, excessReturn: 0.007 },
    { period: '2024 Q3', strategyReturn: 0.038, benchmarkReturn: 0.032, excessReturn: 0.006 },
    { period: '2024 Q4', strategyReturn: 0.072, benchmarkReturn: 0.058, excessReturn: 0.014 },
  ]

  styleExposure.value = [
    { factor: '市值', exposure: 0.15, contribution: 0.023 },
    { factor: '估值', exposure: -0.08, contribution: -0.012 },
    { factor: '动量', exposure: 0.22, contribution: 0.034 },
    { factor: '波动率', exposure: -0.12, contribution: -0.018 },
    { factor: '流动性', exposure: 0.05, contribution: 0.008 },
  ]

  paramSensitivity.value = [
    {
      paramName: '均线周期',
      values: [5, 10, 15, 20, 25, 30],
      returns: [0.28, 0.35, 0.32, 0.29, 0.25, 0.22],
      sharpeRatios: [0.75, 0.82, 0.78, 0.73, 0.68, 0.62],
      maxDrawdowns: [0.14, 0.12, 0.13, 0.15, 0.17, 0.19],
    },
  ]

  costSensitivity.value = [
    { commissionRate: 0.0001, slippageRate: 0.001, totalReturn: 0.38, sharpeRatio: 0.88, numTrades: 156 },
    { commissionRate: 0.0003, slippageRate: 0.002, totalReturn: 0.35, sharpeRatio: 0.82, numTrades: 156 },
    { commissionRate: 0.0005, slippageRate: 0.003, totalReturn: 0.32, sharpeRatio: 0.76, numTrades: 156 },
    { commissionRate: 0.001, slippageRate: 0.005, totalReturn: 0.28, sharpeRatio: 0.68, numTrades: 156 },
    { commissionRate: 0.002, slippageRate: 0.01, totalReturn: 0.22, sharpeRatio: 0.55, numTrades: 156 },
  ]

  periodSensitivity.value = [
    { period: '全样本', startDate: '2023-01-01', endDate: '2024-12-31', totalReturn: 0.3542, sharpeRatio: 0.82, maxDrawdown: 0.1245 },
    { period: '2023', startDate: '2023-01-01', endDate: '2023-12-31', totalReturn: 0.158, sharpeRatio: 0.75, maxDrawdown: 0.098 },
    { period: '2024', startDate: '2024-01-01', endDate: '2024-12-31', totalReturn: 0.168, sharpeRatio: 0.88, maxDrawdown: 0.085 },
    { period: '2023H1', startDate: '2023-01-01', endDate: '2023-06-30', totalReturn: 0.092, sharpeRatio: 0.72, maxDrawdown: 0.065 },
    { period: '2023H2', startDate: '2023-07-01', endDate: '2023-12-31', totalReturn: 0.058, sharpeRatio: 0.68, maxDrawdown: 0.072 },
  ]

  tradeRecords.value = generateMockTrades(50)
  holdingPeriods.value = generateMockHoldingPeriods(25)

  hasData.value = true
}

// === 对外暴露方法 ===

function loadData() {
  loadMockData()
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

.tab-content::-webkit-scrollbar {
  width: 8px;
}

.tab-content::-webkit-scrollbar-track {
  background: transparent;
}

.tab-content::-webkit-scrollbar-thumb {
  background: var(--primary, #2962ff);
  border-radius: 4px;
}

.tab-content::-webkit-scrollbar-thumb:hover {
  background: var(--primary-dark, #1e4fd9);
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
