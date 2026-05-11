/**
 * 统一图表数据源格式
 *
 * 所有图表组件和突变检测工具共享此格式
 */

/** 统一时间序列数据 */
export interface TimeSeries {
  /** X 轴：Unix 时间戳（秒） */
  timestamps: number[]
  /** Y 轴：每列为一个序列，如 [策略收益, 基准收益] */
  values: number[][]
  /** 每列的名称 */
  labels: string[]
  /** 附加元数据 */
  metadata?: Record<string, any>
}

/** 直方图数据（用于分布图） */
export interface HistogramData {
  bins: number[]
  counts: number[]
  metadata?: Record<string, any>
}

/** 图表数据集（一次回测产生的所有图表数据） */
export interface ChartData {
  /** 价格趋势（含买卖信号） */
  price: TimeSeries | null
  /** 累积回报（策略 vs 基准） */
  performance: TimeSeries | null
  /** 回撤曲线 */
  drawdown: TimeSeries | null
  /** 月度收益热力图 */
  monthlyReturns: TimeSeries | null
  /** 收益率分布（直方图 + 正态拟合） */
  distribution: HistogramData | null
  /** 日收益率序列（用于统计计算） */
  dailyReturns: number[] | null
}

/** 突变检测结果 */
export interface AnomalyResult {
  /** 在序列中的索引 */
  index: number
  /** 时间戳 */
  timestamp: number
  /** 当时的值 */
  value: number
  /** 绝对变化量 */
  change: number
  /** 百分比变化 */
  changePct: number
  /** 异常程度（Z-Score） */
  zScore: number
  /** 严重程度 */
  severity: 'high' | 'medium' | 'low'
  /** 所属列名（多列时标识） */
  columnLabel?: string
  /** 人类可读描述 */
  context: string
}

/** 突变检测选项 */
export interface AnomalyOptions {
  /** 检测方法 */
  method: 'slope' | 'volatility' | 'jump' | 'reversal'
  /** 局部窗口大小（用于计算统计量） */
  windowSize: number
  /** 阈值倍数（Z-Score 超过此值视为异常） */
  threshold: number
  /** 最小间隔天数（避免密集报警） */
  minGapDays: number
}

const DEFAULT_OPTIONS: AnomalyOptions = {
  method: 'slope',
  windowSize: 20,
  threshold: 2.5,
  minGapDays: 3,
}

/**
 * 突变检测算法（滚动 Z-Score）
 *
 * 原理：
 * 1. 对每个点，计算其变化量（delta = value[i] - value[i-1]）
 * 2. 取前 windowSize 个变化量，计算局部均值和标准差
 * 3. Z-Score = |delta - local_mean| / local_std
 * 4. 超过阈值倍数的点标记为异常
 */
export function detectAnomalies(
  series: TimeSeries,
  options?: Partial<AnomalyOptions>
): AnomalyResult[] {
  const opts = { ...DEFAULT_OPTIONS, ...options }
  const { timestamps, values, labels } = series
  const allAnomalies: AnomalyResult[] = []

  for (let col = 0; col < values.length; col++) {
    const colValues = values[col]
    const colLabel = labels[col]
    const anomalies = detectSingleSeries(timestamps, colValues, colLabel, opts)
    allAnomalies.push(...anomalies)
  }

  // 按时间排序
  allAnomalies.sort((a, b) => a.timestamp - b.timestamp)

  // 去重：同一天多个列的异常保留最严重的
  return deduplicateByDate(allAnomalies, opts.minGapDays)
}

/**
 * 单列突变检测
 */
function detectSingleSeries(
  timestamps: number[],
  values: number[],
  label: string,
  opts: AnomalyOptions
): AnomalyResult[] {
  const results: AnomalyResult[] = []
  const n = values.length
  if (n < 3) return results

  // 计算变化量序列
  const deltas: number[] = []
  const deltaPcts: number[] = []
  for (let i = 1; i < n; i++) {
    deltas.push(values[i] - values[i - 1])
    deltaPcts.push(values[i - 1] !== 0 ? (values[i] - values[i - 1]) / Math.abs(values[i - 1]) : 0)
  }

  const window = opts.windowSize
  const threshold = opts.threshold
  const minGapMs = opts.minGapDays * 86400000

  let lastAnomalyTime = 0

  for (let i = window; i < deltas.length; i++) {
    // 局部窗口统计
    const windowDeltas = deltas.slice(i - window, i)
    const localMean = mean(windowDeltas)
    const localStd = std(windowDeltas)

    const currentDelta = deltas[i]
    const currentDeltaPct = deltaPcts[i]
    const zScore = localStd > 0 ? Math.abs(currentDelta - localMean) / localStd : 0

    if (zScore > threshold) {
      const ts = timestamps[i + 1] // delta 索引比原始索引 +1

      // 最小间隔过滤
      if (lastAnomalyTime > 0 && ts - lastAnomalyTime < minGapMs) {
        // 如果新的更严重，替换旧的
        if (zScore > (results[results.length - 1]?.zScore ?? 0)) {
          results.pop()
        } else {
          continue
        }
      }

      const severity = zScore > threshold * 2 ? 'high' : zScore > threshold * 1.5 ? 'medium' : 'low'
      const value = values[i + 1]

      let context = ''
      if (opts.method === 'slope') {
        const direction = currentDelta > 0 ? '急涨' : '急跌'
        context = `${label} 在 ${formatDate(ts)} 出现${direction}，变化 ${(currentDeltaPct * 100).toFixed(2)}%，偏离正常波动 ${zScore.toFixed(1)} 倍标准差`
      } else if (opts.method === 'reversal') {
        const prevDelta = deltas[i - 1]
        if (prevDelta * currentDelta < 0) {
          context = `${label} 在 ${formatDate(ts)} 发生趋势反转，前值 ${prevDelta > 0 ? '+' : ''}${(prevDelta * 100).toFixed(2)}% → ${(currentDeltaPct * 100).toFixed(2)}%`
        } else {
          continue // 不是反转
        }
      } else {
        context = `${label} 在 ${formatDate(ts)} 异常变化: ${(currentDeltaPct * 100).toFixed(2)}% (Z=${zScore.toFixed(1)})`
      }

      results.push({
        index: i + 1,
        timestamp: ts,
        value,
        change: currentDelta,
        changePct: currentDeltaPct,
        zScore,
        severity,
        columnLabel: label,
        context,
      })

      lastAnomalyTime = ts
    }
  }

  return results
}

/**
 * 按日期去重：保留同一时间窗内最严重的异常
 */
function deduplicateByDate(anomalies: AnomalyResult[], minGapDays: number): AnomalyResult[] {
  if (anomalies.length === 0) return []

  const deduped: AnomalyResult[] = [anomalies[0]]
  const minGapMs = minGapDays * 86400000

  for (let i = 1; i < anomalies.length; i++) {
    const current = anomalies[i]
    const last = deduped[deduped.length - 1]

    if (current.timestamp - last.timestamp >= minGapMs) {
      deduped.push(current)
    } else if (current.zScore > last.zScore) {
      deduped[deduped.length - 1] = current
    }
  }

  return deduped
}

// ========== 工具函数 ==========

function mean(arr: number[]): number {
  if (arr.length === 0) return 0
  return arr.reduce((s, v) => s + v, 0) / arr.length
}

function std(arr: number[]): number {
  if (arr.length < 2) return 0
  const m = mean(arr)
  const variance = arr.reduce((s, v) => s + (v - m) ** 2, 0) / (arr.length - 1)
  return Math.sqrt(variance)
}

function formatDate(ts: number): string {
  const d = new Date(ts * 1000)
  return `${d.getFullYear()}-${String(d.getMonth() + 1).padStart(2, '0')}-${String(d.getDate()).padStart(2, '0')}`
}

/**
 * 从回测结果构建完整的图表数据集
 */
export function buildChartData(params: {
  /** K 线价格数据 [{datetime, close, ...}] */
  priceData: Array<Record<string, any>>
  /** 买入信号 [symbol, timestamp, quantity, price][] */
  buySignals: [string, number, number, number][]
  /** 卖出信号 [symbol, timestamp, quantity, price][] */
  sellSignals: [string, number, number, number][]
  /** 策略日收益率序列 */
  dailyReturns: number[]
  /** 策略日收益率对应的时间戳 */
  returnDates: number[]
  /** 基准日收益率序列（可选） */
  benchmarkReturns?: number[]
  /** 回撤序列（可选） */
  drawdownSeries?: number[]
  /** 回撤时间戳（可选） */
  drawdownDates?: number[]
}): ChartData {
  const { priceData, buySignals, sellSignals, dailyReturns, returnDates, benchmarkReturns, drawdownSeries, drawdownDates } = params

  // === 1. 价格趋势 ===
  const priceTs: TimeSeries = {
    timestamps: priceData.map(d => d.datetime || d.timestamp),
    values: [priceData.map(d => d.close || d.price)],
    labels: ['价格'],
    metadata: {
      buySignals: buySignals.map(([sym, ts, qty, px]) => ({ symbol: sym, timestamp: ts, quantity: qty, price: px })),
      sellSignals: sellSignals.map(([sym, ts, qty, px]) => ({ symbol: sym, timestamp: ts, quantity: qty, price: px })),
    },
  }

  // === 2. 累积回报 ===
  if (dailyReturns.length > 0) {
    const cumulativeReturns: number[] = []
    let cum = 0
    for (const r of dailyReturns) {
      cum += r
      cumulativeReturns.push(cum)
    }

    const perfValues: number[][] = [cumulativeReturns]
    const perfLabels = ['策略']

    if (benchmarkReturns && benchmarkReturns.length === cumulativeReturns.length) {
      const benchCum: number[] = []
      let bc = 0
      for (const r of benchmarkReturns) {
        bc += r
        benchCum.push(bc)
      }
      perfValues.push(benchCum)
      perfLabels.push('基准')
    }

    return {
      price: priceTs,
      performance: {
        timestamps: returnDates,
        values: perfValues,
        labels: perfLabels,
      },
      drawdown: drawdownSeries && drawdownDates ? {
        timestamps: drawdownDates,
        values: [drawdownSeries],
        labels: ['回撤'],
      } : null,
      monthlyReturns: null, // 需要额外计算，此处留空
      distribution: null,   // 需要从 dailyReturns 计算直方图
      dailyReturns,
    }
  }

  return {
    price: priceTs,
    performance: null,
    drawdown: null,
    monthlyReturns: null,
    distribution: null,
    dailyReturns,
  }
}
