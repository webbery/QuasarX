import { ref, computed } from 'vue'
import type { MarketType, MarketRiskData, StrategyRiskItem, OptionRiskData, StockRiskData, FutureRiskData } from '../types/risk'
import { useIndexQuotes } from '@/composables/useIndexQuotes'

/** 市场风险模拟数据 */
const MARKET_DATA: Record<MarketType, MarketRiskData> = {
  astock: {
    marketType: 'astock',
    indexName: '上证指数',
    indexValue: 0,
    changePercent: 0,
    volatility: 0,
    advanceCount: 0,
    declineCount: 0,
    sentimentIndex: 0,
    sentimentLabel: '',
  },
  future: {
    marketType: 'future',
    indexName: 'IF主力',
    indexValue: 3920.50,
    changePercent: -0.45,
    volatility: 22.10,
    advanceCount: 0,
    declineCount: 0,
    sentimentIndex: 45,
    sentimentLabel: '中性',
  },
  option: {
    marketType: 'option',
    indexName: '50ETF期权',
    indexValue: 2.856,
    changePercent: 3.15,
    volatility: 24.80,
    advanceCount: 0,
    declineCount: 0,
    sentimentIndex: 68,
    sentimentLabel: '贪婪',
  },
  usstock: {
    marketType: 'usstock',
    indexName: 'S&P 500',
    indexValue: 5234.18,
    changePercent: 0.87,
    volatility: 14.30,
    advanceCount: 2156,
    declineCount: 1342,
    sentimentIndex: 72,
    sentimentLabel: '贪婪',
  },
}

/** 策略风险模拟数据 */
const STRATEGIES: StrategyRiskItem[] = [
  {
    id: 'strat_001',
    name: '期权套利',
    description: '基于期权定价偏差的套利策略',
    strategyType: 'option',
    riskLevel: 'critical',
    riskScore: 92,
    var_95: 5.2,
    maxDrawdown: -12.3,
    sharpeRatio: 0.85,
    winRate: 62,
    updatedAt: '2026-04-09 14:30:00',
  },
  {
    id: 'strat_002',
    name: '统计套利',
    description: '基于统计关系的配对交易策略',
    strategyType: 'stock',
    riskLevel: 'high',
    riskScore: 78,
    var_95: 3.8,
    maxDrawdown: -8.1,
    sharpeRatio: 1.21,
    winRate: 58,
    updatedAt: '2026-04-09 14:25:00',
  },
  {
    id: 'strat_003',
    name: '趋势跟踪',
    description: '多周期均线趋势策略',
    strategyType: 'stock',
    riskLevel: 'medium',
    riskScore: 55,
    var_95: 2.1,
    maxDrawdown: -5.4,
    sharpeRatio: 1.56,
    winRate: 45,
    updatedAt: '2026-04-09 14:20:00',
  },
  {
    id: 'strat_004',
    name: '跨期套利',
    description: '期货跨期价差套利',
    strategyType: 'future',
    riskLevel: 'low',
    riskScore: 25,
    var_95: 0.9,
    maxDrawdown: -2.1,
    sharpeRatio: 2.03,
    winRate: 72,
    updatedAt: '2026-04-09 14:15:00',
  },
  {
    id: 'strat_005',
    name: '波动率交易',
    description: '基于隐含波动率与实现波动率差异的交易策略',
    strategyType: 'option',
    riskLevel: 'high',
    riskScore: 82,
    var_95: 4.5,
    maxDrawdown: -9.8,
    sharpeRatio: 1.05,
    winRate: 55,
    updatedAt: '2026-04-09 14:10:00',
  },
  {
    id: 'strat_006',
    name: 'CTA趋势',
    description: '商品期货CTA趋势跟踪策略',
    strategyType: 'future',
    riskLevel: 'medium',
    riskScore: 48,
    var_95: 1.8,
    maxDrawdown: -4.6,
    sharpeRatio: 1.32,
    winRate: 52,
    updatedAt: '2026-04-09 14:05:00',
  },
]

/** 期权特有风险模拟数据 */
const OPTION_RISK_DATA: Record<string, OptionRiskData> = {
  strat_001: {
    greeks: { delta: 0.42, gamma: 0.08, vega: -0.15, theta: -0.05 },
    liquidity: {
      bidAskSpread: 0.015,
      orderBookDepth: 'medium',
      volumeConcentration: '集中',
      impliedVolatility: 22.5,
    },
    expiryRisk: {
      daysToExpiry: 15,
      timeDecayAccelerating: true,
    },
  },
  strat_005: {
    greeks: { delta: -0.18, gamma: 0.12, vega: 0.35, theta: -0.08 },
    liquidity: {
      bidAskSpread: 0.022,
      orderBookDepth: 'low',
      volumeConcentration: '集中',
      impliedVolatility: 28.3,
    },
    expiryRisk: {
      daysToExpiry: 8,
      timeDecayAccelerating: true,
    },
  },
}

/** 股票特有风险模拟数据 */
const STOCK_RISK_DATA: Record<string, StockRiskData> = {
  strat_002: {
    sectorConcentration: 0.65,
    betaWeighted: 1.12,
    turnoverRate: 0.35,
    avgHoldingDays: 18,
  },
  strat_003: {
    sectorConcentration: 0.42,
    betaWeighted: 0.95,
    turnoverRate: 0.22,
    avgHoldingDays: 35,
  },
}

/** 期货特有风险模拟数据 */
const FUTURE_RISK_DATA: Record<string, FutureRiskData> = {
  strat_004: {
    marginRatio: 0.12,
    leverageExposure: 3.2,
    contangoBackwardation: 'contango',
    openInterestChange: 5.6,
  },
  strat_006: {
    marginRatio: 0.10,
    leverageExposure: 2.8,
    contangoBackwardation: 'backwardation',
    openInterestChange: -2.3,
  },
}

export function useMockRiskData() {
  const selectedMarket = ref<MarketType>('astock')

  // 上证指数实时数据
  const { lastPrice: livePrice, changePct: liveChange } = useIndexQuotes('SH000001')

  const marketData = computed((): MarketRiskData => {
    const base = MARKET_DATA[selectedMarket.value]
    if (selectedMarket.value === 'astock') {
      return {
        ...base,
        indexName: '上证指数',
        indexValue: Number(livePrice.value) || 0,
        changePercent: Number(liveChange.value) || 0,
      }
    }
    return base
  })

  const strategies = ref<StrategyRiskItem[]>([...STRATEGIES])

  function getOptionRiskData(strategyId: string): OptionRiskData | null {
    return OPTION_RISK_DATA[strategyId] || null
  }

  function getStockRiskData(strategyId: string): StockRiskData | null {
    return STOCK_RISK_DATA[strategyId] || null
  }

  function getFutureRiskData(strategyId: string): FutureRiskData | null {
    return FUTURE_RISK_DATA[strategyId] || null
  }

  function getStrategyById(id: string): StrategyRiskItem | undefined {
    return strategies.value.find(s => s.id === id)
  }

  return {
    selectedMarket,
    marketData,
    strategies,
    getOptionRiskData,
    getStockRiskData,
    getFutureRiskData,
    getStrategyById,
  }
}
