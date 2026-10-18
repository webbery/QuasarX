// app/src/components/strategy/stability/startSweepStats.ts
// 稳定性 B 组：回测起点不变性 —— 统计判定
//
// 输入是「按固定步长把 backtest.start 向右推移」得到的一串年化收益率：
//   r_0, r_1, ..., r_{n-1}
// 回答两个问题：
//   1. 收益是否产生巨大变化（幅度）
//   2. 这些变化是稳定的（系统性）还是随机的（噪声）
//
// 风险语义（与直觉相反）：
//   随机 + 幅度小 → 绿        策略不挑起点，噪声而已
//   系统性漂移但连续 → 黄      变化有结构，需要归因但不崩
//   断崖（收益符号翻转的孤立跳变）→ 红   起点选对才赚钱，最典型的吃单一行情
//
// 全部为纯函数，不依赖后端，可单测。

export interface StartSensitivityAnalysis {
  count: number
  amplitude: {
    min: number
    max: number
    median: number
    range: number
    /** 相对极差 = range / |median| */
    relativeRange: number | null
    /** 相邻起点之间的最大跳变 */
    maxJump: number
    /** 相邻差分的标准差 */
    diffSigma: number
  }
  structure: {
    /** 相邻起点的记忆强度 */
    acf1: number | null
    /** 置换检验 p 值：越小越说明序列有非随机结构 */
    permutationP: number | null
    trendSlope: number | null
    trendSignificant: boolean
    cliffCount: number
    cliffIndices: number[]
    classification: 'random' | 'systematic' | 'cliff' | 'insufficient'
  }
  /** 年化收益为负的起点占比 */
  negativeRatio: number | null
  verdict: {
    level: 'green' | 'yellow' | 'red' | 'unchecked'
    text: string
  }
}

function mean(values: number[]): number {
  return values.reduce((s, v) => s + v, 0) / values.length
}

function median(values: number[]): number {
  const sorted = [...values].sort((a, b) => a - b)
  const mid = Math.floor(sorted.length / 2)
  return sorted.length % 2 ? sorted[mid] : (sorted[mid - 1] + sorted[mid]) / 2
}

function variance(values: number[]): number {
  if (values.length < 2) return 0
  const m = mean(values)
  return values.reduce((s, v) => s + (v - m) * (v - m), 0) / (values.length - 1)
}

function diffs(values: number[]): number[] {
  return values.slice(1).map((v, i) => v - values[i])
}

/** 确定性 PRNG（mulberry32），保证置换检验可复现 */
function mulberry32(seed: number): () => number {
  let a = seed >>> 0
  return () => {
    a = (a + 0x6d2b79f5) >>> 0
    let t = Math.imul(a ^ (a >>> 15), 1 | a)
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296
  }
}

export function lag1Autocorrelation(values: number[]): number | null {
  if (values.length < 3) return null
  const m = mean(values)
  let num = 0
  let den = 0
  for (let i = 0; i < values.length; i++) {
    den += (values[i] - m) * (values[i] - m)
    if (i < values.length - 1) num += (values[i] - m) * (values[i + 1] - m)
  }
  return den > 1e-12 ? num / den : null
}

/**
 * 置换检验：起点序列天然有邻接结构（相邻起点共享大部分行情），若相邻差分方差
 * 显著小于打乱后的差分方差，说明序列存在可利用的时间结构而非纯噪声。
 * 返回 p 值，越小越有结构。
 */
export function permutationTestP(values: number[], iterations = 1000, seed = 42): number | null {
  if (values.length < 4) return null
  const observed = variance(diffs(values))
  const rng = mulberry32(seed)
  let notGreater = 0
  const pool = [...values]
  for (let it = 0; it < iterations; it++) {
    for (let i = pool.length - 1; i > 0; i--) {
      const j = Math.floor(rng() * (i + 1))
      ;[pool[i], pool[j]] = [pool[j], pool[i]]
    }
    if (variance(diffs(pool)) <= observed) notGreater++
  }
  return notGreater / iterations
}

export interface TrendResult {
  slope: number
  significant: boolean
}

/** r ~ 起点序号 线性回归，斜率超过 2 倍标准误才算显著 */
export function linearTrend(values: number[]): TrendResult | null {
  const n = values.length
  if (n < 3) return null
  const xs = values.map((_, i) => i)
  const mx = mean(xs)
  const my = mean(values)
  let sxy = 0
  let sxx = 0
  for (let i = 0; i < n; i++) {
    sxy += (xs[i] - mx) * (values[i] - my)
    sxx += (xs[i] - mx) * (xs[i] - mx)
  }
  if (sxx <= 1e-12) return { slope: 0, significant: false }
  const slope = sxy / sxx
  const intercept = my - slope * mx
  let ssRes = 0
  for (let i = 0; i < n; i++) {
    const resid = values[i] - (intercept + slope * xs[i])
    ssRes += resid * resid
  }
  const seSlope = Math.sqrt(ssRes / (n - 2) / sxx)
  return { slope, significant: seSlope > 0 && Math.abs(slope) > 2 * seSlope }
}

/**
 * 稳健离散度：1.4826 × MAD。
 * 断崖阈值不能用普通标准差 —— 样本少时标准差会被离群点自身撑大，
 * 离群点永远达不到 3σ，断崖永远检不出来。
 */
export function robustSigma(values: number[]): number {
  if (values.length === 0) return 0
  const med = median(values)
  const deviations = values.map(v => Math.abs(v - med))
  return 1.4826 * median(deviations)
}

/**
 * 断崖检测：相邻跳变超过 3 倍稳健标准差，且该点与前后邻居的收益符号都相反 —— 即孤立翻转。
 * 这是「起点选对了才赚钱」最直接的形态。
 */
export function detectCliffs(values: number[]): number[] {
  const d = diffs(values)
  if (d.length < 2) return []
  const sigma = robustSigma(d)
  if (sigma <= 1e-12) return []
  const cliffs: number[] = []
  for (let i = 0; i < d.length; i++) {
    if (Math.abs(d[i]) <= 3 * sigma) continue
    const prev = values[i]
    const cur = values[i + 1]
    const next = values[i + 2]
    const flipPrev = Math.sign(prev) !== Math.sign(cur) && prev !== 0 && cur !== 0
    const flipNext = next === undefined ? true : Math.sign(cur) !== Math.sign(next) && cur !== 0 && next !== 0
    if (flipPrev && flipNext) cliffs.push(i)
  }
  return cliffs
}

export function analyzeStartSensitivity(returns: number[]): StartSensitivityAnalysis {
  const valid = returns.filter(v => typeof v === 'number' && Number.isFinite(v))
  const n = valid.length

  const empty = (level: 'green' | 'yellow' | 'red' | 'unchecked', text: string): StartSensitivityAnalysis => ({
    count: n,
    amplitude: { min: 0, max: 0, median: 0, range: 0, relativeRange: null, maxJump: 0, diffSigma: 0 },
    structure: {
      acf1: null,
      permutationP: null,
      trendSlope: null,
      trendSignificant: false,
      cliffCount: 0,
      cliffIndices: [],
      classification: 'insufficient',
    },
    negativeRatio: null,
    verdict: { level, text },
  })

  if (n < 4) {
    return empty('unchecked', `有效起点不足 4 个（当前 ${n} 个），无法判定起点不变性。`)
  }

  const d = diffs(valid)
  const med = median(valid)
  const range = Math.max(...valid) - Math.min(...valid)
  const relativeRange = Math.abs(med) > 1e-12 ? range / Math.abs(med) : null
  const maxJump = d.reduce((m, v) => Math.max(m, Math.abs(v)), 0)
  const negativeRatio = valid.filter(v => v < 0).length / n

  const acf1 = lag1Autocorrelation(valid)
  const permutationP = permutationTestP(valid)
  const trend = linearTrend(valid)
  const cliffIndices = detectCliffs(valid)

  let classification: StartSensitivityAnalysis['structure']['classification'] = 'random'
  if (cliffIndices.length > 0) classification = 'cliff'
  else if ((permutationP !== null && permutationP < 0.05) || trend?.significant) classification = 'systematic'

  let level: StartSensitivityAnalysis['verdict']['level'] = 'green'
  let text = ''

  if (cliffIndices.length > 0) {
    level = 'red'
    text = `起点偏移第 ${cliffIndices.map(i => i + 1).join('、')} 档出现孤立跳变且收益符号翻转 —— 起点选对才赚钱，属于吃单一行情。`
  } else if (negativeRatio > 0.5) {
    level = 'red'
    text = `${(negativeRatio * 100).toFixed(0)}% 的起点年化收益为负 —— 策略换个起点就失效。`
  } else if (classification === 'systematic') {
    level = 'yellow'
    text = `收益对起点存在系统性依赖（ACF1=${acf1?.toFixed(3) ?? '-'}，置换检验 p=${permutationP?.toFixed(3) ?? '-'}，趋势斜率=${trend?.slope.toFixed(5) ?? '-'}）—— 变化有结构，需要归因。`
  } else if (relativeRange !== null && relativeRange > 0.5) {
    level = 'yellow'
    text = `未发现时间结构，但起点间波动幅度偏大（相对极差 ${relativeRange.toFixed(2)}）—— 换起点结果差异大但无规律。`
  } else {
    text = `起点偏移过程中收益变化随机且幅度可接受（相对极差 ${relativeRange?.toFixed(2) ?? '-'}，ACF1=${acf1?.toFixed(3) ?? '-'}，置换检验 p=${permutationP?.toFixed(3) ?? '-'}）。`
  }

  return {
    count: n,
    amplitude: {
      min: Math.min(...valid),
      max: Math.max(...valid),
      median: med,
      range,
      relativeRange,
      maxJump,
      diffSigma: Math.sqrt(variance(d)),
    },
    structure: {
      acf1,
      permutationP,
      trendSlope: trend?.slope ?? null,
      trendSignificant: trend?.significant ?? false,
      cliffCount: cliffIndices.length,
      cliffIndices,
      classification,
    },
    negativeRatio,
    verdict: { level, text },
  }
}
