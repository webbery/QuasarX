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
      <div v-if="!hasData && !loading" class="empty-state">
        <i class="fas fa-chart-line"></i>
        <p>暂无复盘数据</p>
        <span>该策略暂无交易记录，请确认策略已运行并产生持仓数据</span>
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, onMounted, watch } from 'vue'
import axios from 'axios'
import BasicMetricsTab from './tabs/BasicMetricsTab.vue'
import AttributionTab from './tabs/AttributionTab.vue'
import SensitivityTab from './tabs/SensitivityTab.vue'
import TradeDetailTab from './tabs/TradeDetailTab.vue'

interface Props {
  strategyName?: string
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
const loading = ref(false)

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

// === 服务端数据加载 ===

async function loadData() {
  clearData()
  if (!props.strategyName) return

  loading.value = true
  try {
    // 并行请求交易明细和绩效指标
    const [tradeResp, perfResp] = await Promise.all([
      axios.get('/v0/strategy/trade', { params: { name: props.strategyName } }),
      axios.get('/v0/strategy/performance', { params: { name: props.strategyName } }),
    ])

    // 处理交易明细
    const tradeData = tradeResp.data
    if (tradeData.trades && tradeData.trades.length > 0) {
      mapTradeData(tradeData)
      hasData.value = true
    }

    // 处理绩效指标
    const perfData = perfResp.data
    if (perfData.metrics) {
      mapPerformanceData(perfData)
      hasData.value = true
    }
  } catch (e: any) {
    console.error('[ReviewPanel] 加载数据失败:', e.message)
  } finally {
    loading.value = false
  }
}

function mapTradeData(data: any) {
  const trades: TradeRecord[] = []
  const holdings: HoldingPeriod[] = []

  // 映射 trades
  if (Array.isArray(data.trades)) {
    data.trades.forEach((t: any, i: number) => {
      trades.push({
        id: `t${i}`,
        symbol: t.symbol,
        direction: t.direction,
        price: t.price,
        quantity: t.quantity,
        amount: t.price * t.quantity,
        commission: 0,
        timestamp: t.date,
        pnl: t.pnl ?? undefined,
      })
    })
  }

  // 映射 holdings（排除未平仓的）
  if (Array.isArray(data.holdings)) {
    data.holdings.forEach((h: any) => {
      if (h.open || !h.sell_date) return
      holdings.push({
        symbol: h.symbol,
        buyDate: h.buy_date,
        sellDate: h.sell_date,
        buyPrice: h.buy_price,
        sellPrice: h.sell_price,
        holdingDays: h.holding_days || 1,
        pnl: h.pnl || 0,
        pnlPercent: h.pnl_pct || 0,
      })
    })
  }

  tradeRecords.value = trades
  holdingPeriods.value = holdings

  // 设置回测时间范围
  if (data.trades && data.trades.length > 0) {
    const dates = data.trades.map((t: any) => t.date).sort()
    backtestInfo.value = {
      startDate: dates[0],
      endDate: dates[dates.length - 1],
      initialCapital: 1000000,
    }
  }
}

function mapPerformanceData(data: any) {
  const m = data.metrics
  metricsData.value = {
    total_return: m.total_return || 0,
    annual_return: m.annual_return || 0,
    volatility: m.annual_volatility || 0,
    sharp: m.sharpe_ratio || 0,
    max_drawdown: m.max_drawdown || 0,
    win_rate: m.win_rate || 0,
    calmar_ratio: m.calmar_ratio || 0,
  }

  if (data.initial_capital && data.final_value) {
    backtestInfo.value = {
      startDate: backtestInfo.value?.startDate || '-',
      endDate: backtestInfo.value?.endDate || '-',
      initialCapital: data.initial_capital,
    }
  }
}

function reset() {
  clearData()
}

defineExpose({ loadData, reset })

// === 监听策略名称变化 ===

watch(() => props.strategyName, (newName) => {
  if (newName) {
    loadData()
  } else {
    clearData()
  }
})

// === 生命周期 ===

onMounted(() => {
  console.info('[ReviewPanel] 组件已挂载')
  if (props.strategyName) {
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
