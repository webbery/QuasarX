/**
 * 风控面板类型定义
 */

export type MarketType = 'astock' | 'future' | 'option' | 'usstock'
export type RiskLevel = 'low' | 'medium' | 'high' | 'critical'
export type StrategyType = 'option' | 'stock' | 'future' | 'mixed'

/** 市场风险概览数据 */
export interface MarketRiskData {
  marketType: MarketType
  indexName: string
  indexValue: number
  changePercent: number
  volatility: number
  advanceCount: number
  declineCount: number
  sentimentIndex: number     // 0-100
  sentimentLabel: string     // '极度恐惧' | '恐惧' | '中性' | '贪婪' | '极度贪婪'
}

/** 策略风险列表项 */
export interface StrategyRiskItem {
  id: string
  name: string
  description: string
  strategyType: StrategyType
  riskLevel: RiskLevel
  riskScore: number          // 0-100，用于排序
  var_95: number            // 95% VaR
  maxDrawdown: number       // 最大回撤
  sharpeRatio: number       // 夏普比率
  winRate: number           // 胜率
  updatedAt: string
}

/** 风险指标配置 */
export interface RiskMetricConfig {
  key: string
  label: string
  enabled: boolean
  warningThreshold: number
  criticalThreshold: number
  unit: string
}

/** 策略风险配置 */
export interface StrategyRiskConfig {
  strategyId: string
  metrics: RiskMetricConfig[]
  enabled: boolean
}

/** 期权 Greeks */
export interface OptionGreeks {
  delta: number
  gamma: number
  vega: number
  theta: number
}

/** 期权流动性风险 */
export interface OptionLiquidityRisk {
  bidAskSpread: number
  orderBookDepth: 'low' | 'medium' | 'high'
  volumeConcentration: '分散' | '集中'
  impliedVolatility: number
}

/** 期权到期日风险 */
export interface OptionExpiryRisk {
  daysToExpiry: number
  timeDecayAccelerating: boolean
}

/** 期权特有风险数据 */
export interface OptionRiskData {
  greeks: OptionGreeks
  liquidity: OptionLiquidityRisk
  expiryRisk: OptionExpiryRisk
}

/** 股票特有风险数据 */
export interface StockRiskData {
  sectorConcentration: number    // 行业集中度
  betaWeighted: number           // Beta 加权
  turnoverRate: number           // 换手率
  avgHoldingDays: number         // 平均持仓天数
}

/** 期货特有风险数据 */
export interface FutureRiskData {
  marginRatio: number            // 保证金比例
  leverageExposure: number       // 杠杆暴露
  contangoBackwardation: 'contango' | 'backwardation' | 'flat'
  openInterestChange: number     // 持仓量变化
}
