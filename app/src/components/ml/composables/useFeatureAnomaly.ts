import type { Anomaly, FeatureSeries } from './useMLState'

export interface AnomalyConfig {
  nanRunMin: number       // NaN 连续段最小长度
  jumpEpsilon: number     // 跳 0 的接近 0 阈值
  jumpHoldMin: number     // 跳 0 后保持低值的最小天数
  staleMin: number        // 长期停滞最小天数
}

export const DEFAULT_ANOMALY_CONFIG: AnomalyConfig = {
  nanRunMin: 3,
  jumpEpsilon: 1e-6,
  jumpHoldMin: 3,
  staleMin: 5,
}

const isNum = (v: number | null): v is number => v !== null && Number.isFinite(v)

/**
 * 把 "symbol.feature" 拆成 {symbol, feature}，与现有 FeatureInspectionPanel 一致
 */
export function parseFeatureName(name: string): { symbol: string; feature: string } {
  // symbol 格式固定为 "{exchange}.{code}"（如 sz.000001），只有一个点
  // 找第二个点作为 symbol/feature 的分界线
  // "sz.000001.ma20"          → symbol="sz.000001", feature="ma20"
  // "sz.000001.emd.nimf_0"    → symbol="sz.000001", feature="emd.nimf_0"
  const firstDot = name.indexOf('.')
  if (firstDot === -1) return { symbol: '', feature: name }
  const secondDot = name.indexOf('.', firstDot + 1)
  if (secondDot === -1) {
    // 只有一个点：可能是 "exchange.code" 无 feature，或 "symbol.feature" 旧格式
    return { symbol: name.slice(0, firstDot), feature: name.slice(firstDot + 1) }
  }
  return { symbol: name.slice(0, secondDot), feature: name.slice(secondDot + 1) }
}

/**
 * 对单个时序做异常检测
 */
export function detectAnomalies(
  series: FeatureSeries,
  symbol: string,
  feature: string,
  config: AnomalyConfig = DEFAULT_ANOMALY_CONFIG,
): Anomaly[] {
  const values = series.data[`${symbol}.${feature}`]
  if (!values) return []
  const dates = series.dates
  const anomalies: Anomaly[] = []

  // 跳过预热期：找到第一个有效值的位置
  // 预热期（如 MA(20) 前 19 个 bar）的 NaN 是指标计算所需的正常现象，不应报告为异常
  let startIdx = 0
  while (startIdx < values.length && !isNum(values[startIdx])) {
    startIdx++
  }
  if (startIdx >= values.length) return []  // 全部是 NaN，无法分析

  // ── NaN 段（从预热结束开始检测） ──
  let runStart = -1
  const flushNanRun = (endIdx: number) => {
    if (runStart < 0) return
    const length = endIdx - runStart + 1
    if (length >= config.nanRunMin) {
      anomalies.push({
        type: 'nan_run',
        symbol, feature,
        start_date: dates[runStart],
        end_date: dates[endIdx],
        length,
        severity: length >= 10 ? 'high' : length >= 5 ? 'mid' : 'low',
        detail: `${length} 天连续 NaN`,
      })
    }
    runStart = -1
  }
  for (let i = startIdx; i < values.length; i++) {
    if (!isNum(values[i])) {
      if (runStart < 0) runStart = i
    } else {
      flushNanRun(i - 1)
    }
  }
  flushNanRun(values.length - 1)

  // ── 跳 0 突变：|prev| > eps && |cur| < eps，且后续 K 天保持 ≤ eps ──
  for (let i = Math.max(1, startIdx); i < values.length; i++) {
    const prev = values[i - 1]
    const cur = values[i]
    if (!isNum(prev) || !isNum(cur)) continue
    if (Math.abs(prev) <= config.jumpEpsilon) continue
    if (Math.abs(cur) > config.jumpEpsilon) continue
    // cur 已跌到接近 0，看后续 K 天是否保持
    let holdLen = 1
    for (let k = i + 1; k < values.length && k < i + config.jumpHoldMin + 1; k++) {
      const vk = values[k]
      if (!isNum(vk) || Math.abs(vk) > config.jumpEpsilon) break
      holdLen++
    }
    if (holdLen >= config.jumpHoldMin) {
      anomalies.push({
        type: 'jump_to_zero',
        symbol, feature,
        start_date: dates[i],
        end_date: dates[Math.min(i + config.jumpHoldMin, dates.length) - 1],
        length: holdLen,
        severity: 'high',
        detail: `从 ${prev.toFixed(3)} 突变为 ${cur.toFixed(3)}，持续 ${holdLen} 天接近 0`,
      })
      i += config.jumpHoldMin  // 跳过已记录的窗口
    }
  }

  // ── 长期停滞：连续 N 天值完全相同 ──
  let staleStart = -1
  let lastVal: number | null = null
  const flushStale = (endIdx: number) => {
    if (staleStart < 0) return
    const length = endIdx - staleStart + 1
    if (length >= config.staleMin) {
      anomalies.push({
        type: 'stale_run',
        symbol, feature,
        start_date: dates[staleStart],
        end_date: dates[endIdx],
        length,
        severity: 'mid',
        detail: `连续 ${length} 天值不变（=${lastVal?.toFixed(3)}）`,
      })
    }
    staleStart = -1
    lastVal = null
  }
  for (let i = startIdx; i < values.length; i++) {
    const v = values[i]
    if (isNum(v)) {
      if (lastVal !== null && v === lastVal) {
        if (staleStart < 0) staleStart = i - 1
      } else {
        if (staleStart >= 0) flushStale(i - 1)
        lastVal = v
      }
    } else {
      flushStale(i - 1)
    }
  }
  flushStale(values.length - 1)

  return anomalies
}

/**
 * 对 series 里所有 (symbol, feature) 都做检测，结果按严重度+长度排序
 */
export function detectAllAnomalies(
  series: FeatureSeries,
  config: AnomalyConfig = DEFAULT_ANOMALY_CONFIG,
): Anomaly[] {
  const out: Anomaly[] = []
  for (const fullName of Object.keys(series.data)) {
    const { symbol, feature } = parseFeatureName(fullName)
    out.push(...detectAnomalies(series, symbol, feature, config))
  }
  const sevRank = { high: 0, mid: 1, low: 2 }
  out.sort((a, b) => sevRank[a.severity] - sevRank[b.severity] || b.length - a.length)
  return out
}
