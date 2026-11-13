// app/src/composables/useStrategyTrades.ts
// 策略成交明细数据源（/v0/strategy/trade）
// 同时服务「历史复盘」的图表与「下单管理」的交易明细记录表

import { ref } from 'vue'
import axios from 'axios'

export interface StrategyTrade {
  id: string
  symbol: string
  direction: 'buy' | 'sell'
  price: number
  quantity: number
  amount: number
  commission: number
  timestamp: string
  positionAfter?: number
  pnl?: number
}

export interface StrategyHolding {
  symbol: string
  buyDate: string
  sellDate: string
  buyPrice: number
  sellPrice: number
  quantity: number
  holdingDays: number
  pnl: number
  pnlPercent: number
}

export interface TradeSummary {
  totalTrades: number
  completedHoldings: number
  winCount: number
  lossCount: number
  winRate: number
  totalPnl: number
  avgPnl: number
}

function emptySummary(): TradeSummary {
  return {
    totalTrades: 0,
    completedHoldings: 0,
    winCount: 0,
    lossCount: 0,
    winRate: 0,
    totalPnl: 0,
    avgPnl: 0,
  }
}

/** 服务端原始成交记录 → 前端结构 */
function mapTrades(raw: unknown): StrategyTrade[] {
  if (!Array.isArray(raw)) return []
  return raw.map((t: any, i: number) => {
    const price = Number(t.price) || 0
    const quantity = Number(t.quantity) || 0
    return {
      id: `t${i}`,
      symbol: t.symbol ?? '',
      direction: t.direction === 'sell' ? 'sell' : 'buy',
      price,
      quantity,
      amount: price * quantity,
      commission: Number(t.commission) || 0,
      timestamp: t.date ?? '',
      positionAfter: t.position_after,
      pnl: t.pnl === undefined || t.pnl === null ? undefined : Number(t.pnl),
    }
  })
}

/** 服务端原始持仓记录 → 前端结构（排除未平仓） */
function mapHoldings(raw: unknown): StrategyHolding[] {
  if (!Array.isArray(raw)) return []
  return raw
    .filter((h: any) => !h.open && h.sell_date)
    .map((h: any) => ({
      symbol: h.symbol ?? '',
      buyDate: h.buy_date ?? '',
      sellDate: h.sell_date ?? '',
      buyPrice: Number(h.buy_price) || 0,
      sellPrice: Number(h.sell_price) || 0,
      quantity: Number(h.quantity) || 0,
      holdingDays: h.holding_days || 1,
      pnl: Number(h.pnl) || 0,
      pnlPercent: Number(h.pnl_pct) || 0,
    }))
}

function mapSummary(raw: any): TradeSummary {
  if (!raw) return emptySummary()
  return {
    totalTrades: Number(raw.total_trades) || 0,
    completedHoldings: Number(raw.completed_holdings) || 0,
    winCount: Number(raw.win_count) || 0,
    lossCount: Number(raw.loss_count) || 0,
    winRate: Number(raw.win_rate) || 0,
    totalPnl: Number(raw.total_pnl) || 0,
    avgPnl: Number(raw.avg_pnl) || 0,
  }
}

export function useStrategyTrades() {
  const trades = ref<StrategyTrade[]>([])
  const holdings = ref<StrategyHolding[]>([])
  const summary = ref<TradeSummary>(emptySummary())
  const loading = ref(false)
  const error = ref<string | null>(null)

  // 请求序号：切换策略后丢弃过期响应，避免旧数据覆盖新策略
  let requestSeq = 0

  /** 拉取指定策略的成交明细与持仓周期 */
  async function fetchTrades(strategyName?: string): Promise<StrategyTrade[]> {
    if (!strategyName) {
      reset()
      return []
    }

    const seq = ++requestSeq
    loading.value = true
    error.value = null
    try {
      const resp = await axios.get('/v0/strategy/trade', { params: { name: strategyName } })
      if (seq !== requestSeq) return trades.value // 过期响应，丢弃
      const data = resp.data ?? {}
      trades.value = mapTrades(data.trades)
      holdings.value = mapHoldings(data.holdings)
      summary.value = mapSummary(data.summary)
      return trades.value
    } catch (e: any) {
      if (seq !== requestSeq) return trades.value
      error.value = e?.response?.data?.error || e?.message || '交易记录加载失败'
      trades.value = []
      holdings.value = []
      summary.value = emptySummary()
      return []
    } finally {
      if (seq === requestSeq) loading.value = false
    }
  }

  /** 清空数据（切换策略 / 卸载） */
  function reset() {
    requestSeq++
    trades.value = []
    holdings.value = []
    summary.value = emptySummary()
    error.value = null
    loading.value = false
  }

  return {
    trades,
    holdings,
    summary,
    loading,
    error,
    fetchTrades,
    reset,
  }
}
