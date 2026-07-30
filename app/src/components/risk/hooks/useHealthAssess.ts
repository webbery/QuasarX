/**
 * 策略健康度评估（四维加权评分）
 *
 * 维度：IR（收益效率）、回撤控制、VaR 占用、CUSUM 结构稳定
 * 权重按策略类型差异化（从 winRate + avgWinLossRatio 自动推断）
 */

export type InferredStrategyType = 'trend' | 'mean_reversion' | 'balanced'

export interface HealthStatus {
  level: 'excellent' | 'good' | 'warning' | 'critical'
  label: string
  icon: string
  color: string
  suggestion: string
  score: number            // 0~100 综合评分
  strategyType: InferredStrategyType
}

export interface HealthInput {
  ir: number
  maxDrawdown: number      // 正数，如 0.15 表示 15%
  varRatio: number         // VaR / 总资金，正数
  driftRatio: number       // CUSUM 归一化漂移 0~1
  winRate: number          // 0~1
  avgWinLossRatio: number  // 平均盈利 / 平均亏损
}

// 权重表：[IR, 回撤, VaR, CUSUM]
const WEIGHTS: Record<InferredStrategyType, [number, number, number, number]> = {
  trend:         [0.20, 0.35, 0.15, 0.30],
  mean_reversion:[0.25, 0.20, 0.30, 0.25],
  balanced:      [0.25, 0.25, 0.25, 0.25],
}

export function inferStrategyType(winRate: number, avgWinLossRatio: number): InferredStrategyType {
  if (winRate < 0.45 && avgWinLossRatio > 2.0) return 'trend'
  if (winRate > 0.60) return 'mean_reversion'
  return 'balanced'
}

function clamp(v: number, lo: number, hi: number): number {
  return Math.max(lo, Math.min(hi, v))
}

function scoreIR(ir: number): number {
  if (ir < 0) return 0
  if (ir < 0.5) return 30 + (ir / 0.5) * 40   // 0→30, 0.5→70
  if (ir < 1.0) return 70 + ((ir - 0.5) / 0.5) * 30  // 0.5→70, 1→100
  return 100
}

function scoreDrawdown(dd: number): number {
  if (dd <= 0.05) return 100
  if (dd <= 0.10) return 100 - ((dd - 0.05) / 0.05) * 30   // 5%→100, 10%→70
  if (dd <= 0.15) return 70 - ((dd - 0.10) / 0.05) * 30    // 10%→70, 15%→40
  return Math.max(0, 40 - ((dd - 0.15) / 0.05) * 40)       // 15%→40, 20%→0
}

function scoreVaRUtilization(varRatio: number): number {
  if (varRatio <= 0.01) return 100
  if (varRatio <= 0.02) return 100 - ((varRatio - 0.01) / 0.01) * 30
  if (varRatio <= 0.03) return 70 - ((varRatio - 0.02) / 0.01) * 30
  return Math.max(0, 40 - ((varRatio - 0.03) / 0.02) * 40)
}

function scoreCUSUMStability(driftRatio: number): number {
  if (driftRatio <= 0.3) return 100
  if (driftRatio <= 0.6) return 100 - ((driftRatio - 0.3) / 0.3) * 30
  if (driftRatio <= 0.9) return 70 - ((driftRatio - 0.6) / 0.3) * 30
  return Math.max(0, 40 - ((driftRatio - 0.9) / 0.1) * 40)
}

function scoreToStatus(score: number): HealthStatus['level'] {
  if (score >= 80) return 'excellent'
  if (score >= 60) return 'good'
  if (score >= 40) return 'warning'
  return 'critical'
}

const LEVEL_META: Record<HealthStatus['level'], { label: string; icon: string; color: string; suggestion: string }> = {
  excellent: { label: '优秀', icon: '🟢', color: '#00c853', suggestion: '维持' },
  good:      { label: '良好', icon: '🔵', color: '#2962ff', suggestion: '维持' },
  warning:   { label: '警戒', icon: '🟠', color: '#ff9800', suggestion: '观察' },
  critical:  { label: '危险', icon: '🔴', color: '#ff1744', suggestion: '考虑关闭' },
}

export function assessHealth(input: HealthInput): HealthStatus {
  const type = inferStrategyType(input.winRate, input.avgWinLossRatio)
  const [wIR, wDD, wVaR, wCUSUM] = WEIGHTS[type]

  const sIR = scoreIR(input.ir)
  const sDD = scoreDrawdown(input.maxDrawdown)
  const sVaR = scoreVaRUtilization(input.varRatio)
  const sCUSUM = scoreCUSUMStability(input.driftRatio)

  const score = clamp(
    Math.round(wIR * sIR + wDD * sDD + wVaR * sVaR + wCUSUM * sCUSUM),
    0, 100
  )

  const level = scoreToStatus(score)

  // IR < 0 强制 critical
  const finalLevel = input.ir < 0 ? 'critical' : level
  const meta = LEVEL_META[finalLevel]

  return {
    level: finalLevel,
    label: meta.label,
    icon: meta.icon,
    color: meta.color,
    suggestion: input.ir < 0 ? '立即关闭' : meta.suggestion,
    score,
    strategyType: type,
  }
}
