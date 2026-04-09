import type { RiskLevel, StrategyRiskItem } from '../types/risk'

/** 风险等级排序权重 */
const RISK_LEVEL_ORDER: Record<RiskLevel, number> = {
  critical: 4,
  high: 3,
  medium: 2,
  low: 1,
}

/**
 * 按风险等级和风险评分排序策略
 * 规则：风险等级高的在前，同等级按风险评分降序
 */
export function sortStrategiesByRisk(strategies: StrategyRiskItem[]): StrategyRiskItem[] {
  return [...strategies].sort((a, b) => {
    const levelDiff = RISK_LEVEL_ORDER[b.riskLevel] - RISK_LEVEL_ORDER[a.riskLevel]
    if (levelDiff !== 0) return levelDiff
    return b.riskScore - a.riskScore
  })
}

/**
 * 获取风险等级对应的 bar 数量（1-5）
 */
export function riskBarCount(level: RiskLevel): number {
  return RISK_LEVEL_ORDER[level]
}

/**
 * 获取风险等级对应的 CSS 类名
 */
export function riskLevelClass(level: RiskLevel): string {
  const map: Record<RiskLevel, string> = {
    critical: 'risk-critical',
    high: 'risk-high',
    medium: 'risk-medium',
    low: 'risk-low',
  }
  return map[level]
}

/**
 * 获取风险等级对应的标签类型（Element Plus）
 */
export function riskLevelTagType(level: RiskLevel): 'danger' | 'warning' | 'info' | 'success' {
  const map: Record<RiskLevel, 'danger' | 'warning' | 'info' | 'success'> = {
    critical: 'danger',
    high: 'warning',
    medium: 'info',
    low: 'success',
  }
  return map[level]
}

/**
 * 获取风险等级中文标签
 */
export function riskLevelLabel(level: RiskLevel): string {
  const map: Record<RiskLevel, string> = {
    critical: '极高风险',
    high: '高风险',
    medium: '中风险',
    low: '低风险',
  }
  return map[level]
}

/**
 * 获取策略类型中文标签
 */
export function strategyTypeLabel(type: StrategyRiskItem['strategyType']): string {
  const map: Record<StrategyRiskItem['strategyType'], string> = {
    option: '期权',
    stock: '股票',
    future: '期货',
    mixed: '混合',
  }
  return map[type]
}

/**
 * 市场类型中文标签
 */
export function marketTypeLabel(type: string): string {
  const map: Record<string, string> = {
    astock: 'A股',
    future: '期货',
    option: '期权',
    usstock: '美股',
  }
  return map[type] || type
}
