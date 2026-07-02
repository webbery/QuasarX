/**
 * 策略健康度评估
 *
 * 结合信息比率 (IR) + CUSUM 信号，评估策略当前状态的健康程度。
 *
 * 评估规则：
 *   IR < 0                    → 🔴 亏损，立即关闭
 *   0 ≤ IR < 0.5 + CUSUM S-   → 🔴 恶化，考虑关闭
 *   0 ≤ IR < 0.5              → 🟠 警戒，观察
 *   IR ≥ 0.5 + CUSUM S+       → 🟢 改善，可加仓
 *   IR ≥ 1.0                  → 🟢 优秀，维持
 *   其他                      → 🔵 良好，维持
 */

export interface HealthStatus {
  level: 'excellent' | 'good' | 'warning' | 'critical'
  label: string
  icon: string
  color: string
  suggestion: string
}

export function assessHealth(ir: number, cusumSignal: number): HealthStatus {
  // IR < 0：亏损策略
  if (ir < 0) {
    return {
      level: 'critical',
      label: '亏损',
      icon: '🔴',
      color: '#ff1744',
      suggestion: '立即关闭',
    }
  }

  // IR 低 + CUSUM S- 触发：恶化
  if (ir < 0.5 && cusumSignal === -1) {
    return {
      level: 'critical',
      label: '恶化',
      icon: '🔴',
      color: '#ff1744',
      suggestion: '考虑关闭',
    }
  }

  // IR 低但未触发：警戒
  if (ir < 0.5) {
    return {
      level: 'warning',
      label: '警戒',
      icon: '🟠',
      color: '#ff9800',
      suggestion: '观察',
    }
  }

  // IR 高 + CUSUM S+ 触发：改善趋势
  if (ir >= 0.5 && cusumSignal === 1) {
    return {
      level: 'excellent',
      label: '改善',
      icon: '🟢',
      color: '#00c853',
      suggestion: '可加仓',
    }
  }

  // IR ≥ 1.0：优秀
  if (ir >= 1.0) {
    return {
      level: 'excellent',
      label: '优秀',
      icon: '🟢',
      color: '#00c853',
      suggestion: '维持',
    }
  }

  // 其他：良好
  return {
    level: 'good',
    label: '良好',
    icon: '🔵',
    color: '#2962ff',
    suggestion: '维持',
  }
}
