/**
 * 数据层 resolver
 *
 * 产物里的 data 是**快照**，但用户可能想换基准、换标的。
 * 数据层控件（layer: 'data'）变更时，渲染层调用这里的 resolver 回源换 data。
 *
 * resolver 是**代码**，不是 LLM 编的 JSON —— 与 P0「动作参数由工具生成」同一原则。
 * LLM 只能影响 params（标的代码、基准代码等），实际取数逻辑固定在这里。
 *
 * 复用现有数据源，不重写取数：
 *   - useHistoryStore().getLatestBacktestResult() / loadBacktestResult(versionId)
 *   - getBenchmark(symbol, start, end)        —— lib/tickflow.ts
 *   - POST /v0/stocks/history                 —— 同 useChartData.ts 的请求形状
 */

import axios from 'axios'
import { getBenchmark } from '@/lib/tickflow'
import { useHistoryStore } from '@/stores/history'
import type { UiControlDef, UiResolver } from './types'

const resolvers = new Map<string, UiResolver>()

/** 注册一个数据层 resolver */
export function registerResolver(id: string, fn: UiResolver): void {
  resolvers.set(id, fn)
}

/** 按 id 查找 resolver */
export function getResolver(id: string): UiResolver | undefined {
  return resolvers.get(id)
}

/** 已登记的 resolver id */
export function listResolverIds(): string[] {
  return [...resolvers.keys()]
}

/* ============================================================================
 * 常用基准
 * ========================================================================== */

/** 可选基准（工具/resolver 生成，不让 LLM 现场编） */
export const BENCHMARK_OPTIONS: Array<{ value: string; label: string }> = [
  { value: 'SH000001', label: '上证指数' },
  { value: 'SH000300', label: '沪深300' },
  { value: 'SH000905', label: '中证500' },
  { value: 'SZ399001', label: '深证成指' },
  { value: 'SZ399006', label: '创业板指' },
]

/** 默认基准 */
export const DEFAULT_BENCHMARK = 'SH000001'

/* ============================================================================
 * 工具
 * ========================================================================== */

/** 把数字数组转成列式的一列 */
function toColumn(
  n: string,
  v: Array<number | string | null>,
  extra?: { u?: string; tone?: 'good' | 'bad' | 'neutral' },
) {
  return { n, v, ...(extra ?? {}) }
}

/** 时间戳（秒）→ 'MM-DD' */
function shortDate(ts: number): string {
  const d = new Date(ts > 1e11 ? ts : ts * 1000)
  const mm = String(d.getMonth() + 1).padStart(2, '0')
  const dd = String(d.getDate()).padStart(2, '0')
  return `${mm}-${dd}`
}

/** 累计收益：把日收益率累乘成净值曲线 */
function toCumulative(returns: number[]): number[] {
  let acc = 1
  return returns.map((r) => {
    acc *= 1 + (Number.isFinite(r) ? r : 0)
    return acc - 1
  })
}

/** 从回测结果里取出日收益率与时间戳 */
function extractDailySeries(result: any): { returns: number[]; timestamps: number[] } {
  const returns = Array.isArray(result?.dailyReturns) ? result.dailyReturns : []
  const timestamps = Array.isArray(result?.dailyDates) ? result.dailyDates : []
  return { returns, timestamps }
}

/** 从回测结果里推算时间范围（用于取基准） */
function inferRange(result: any): { start: Date; end: Date } {
  const { timestamps } = extractDailySeries(result)
  const all = [...timestamps]
  for (const s of result?.buy ?? []) all.push(s[1])
  for (const s of result?.sell ?? []) all.push(s[1])
  if (!all.length) {
    const now = Date.now()
    return { start: new Date(now - 365 * 86400000), end: new Date(now) }
  }
  const min = Math.min(...all)
  const max = Math.max(...all)
  return { start: new Date(min * 1000), end: new Date(max * 1000) }
}

/** 拉取某标的的日线（后复权） */
async function fetchSymbolPrices(
  symbol: string,
  start: Date,
  end: Date,
): Promise<Array<{ ts: number; close: number }>> {
  const server = localStorage.getItem('remote')
  const token = localStorage.getItem('token')
  if (!server) return []

  const params = {
    id: symbol,
    type: '1d',
    start: Math.floor(start.getTime() / 1000),
    end: Math.floor(end.getTime() / 1000),
    right: 1, // 后复权
  }

  const response = await axios.get(`https://${server}/v0/stocks/history`, {
    params,
    headers: { Authorization: token },
  })
  if (!Array.isArray(response.data)) return []

  const out: Array<{ ts: number; close: number }> = []
  for (const oclhv of response.data) {
    const dt = oclhv['datetime']
    if (dt === undefined) continue
    out.push({ ts: dt, close: Number(oclhv['close']) })
  }
  return out
}

/* ============================================================================
 * 具体 resolver
 * ========================================================================== */

/**
 * 回测累计收益 vs 基准。
 *
 * args:
 *   - version_id?   指定版本；缺省用最近一次回测
 *   - benchmark?    基准代码；缺省用 SH000001
 */
registerResolver('backtest_performance', async (args) => {
  const history = useHistoryStore()
  const versionId = args.version_id as string | undefined
  const benchmark = (args.benchmark as string) || DEFAULT_BENCHMARK

  const result = versionId
    ? await history.loadBacktestResult(versionId)
    : await history.getLatestBacktestResult()
  if (!result) {
    throw new Error('没有可用的回测结果，请先执行一次回测')
  }

  const { returns, timestamps } = extractDailySeries(result)
  const cumulative = toCumulative(returns)
  const cols = [toColumn('策略收益', cumulative)]

  // 回撤：累计收益 - 历史峰值
  let peak = -Infinity
  const drawdown = cumulative.map((v) => {
    if (v > peak) peak = v
    return v - peak
  })
  cols.push({ ...toColumn('回撤', drawdown), tone: 'bad' })

  // 基准：按回测时间范围取基准日线，算同区间累计收益
  try {
    const { start, end } = inferRange(result)
    const bench = await getBenchmark(benchmark, start, end)
    const benchReturns = bench.data.map((d, i) => {
      const prev = bench.data[i - 1]
      return prev ? (d.close - prev.close) / prev.close : 0
    })
    cols.push(toColumn(`基准收益(${benchmark})`, toCumulative(benchReturns)))
  } catch (e) {
    console.warn('[charts/resolvers] 基准数据获取失败，只画策略曲线', e)
  }

  return {
    data: {
      x: timestamps,
      cols,
      x_label: '日期',
      y_label: '累计收益',
      y_format: 'percent',
    },
    controls: [
      {
        key: 'benchmark',
        label: '基准',
        kind: 'select',
        layer: 'data',
        options: BENCHMARK_OPTIONS,
        default: benchmark,
      },
    ],
  }
})

/**
 * 某标的的价格曲线 + 买卖信号标记。
 *
 * args:
 *   - symbol     标的代码（必填）
 *   - version_id? 从回测结果里取信号
 */
registerResolver('symbol_price', async (args) => {
  const symbol = args.symbol as string
  if (!symbol) throw new Error('缺少 symbol 参数')

  const history = useHistoryStore()
  const versionId = args.version_id as string | undefined
  const result = versionId
    ? await history.loadBacktestResult(versionId)
    : await history.getLatestBacktestResult()

  const { start, end } = result ? inferRange(result) : {
    start: new Date(Date.now() - 365 * 86400000),
    end: new Date(),
  }

  const prices = await fetchSymbolPrices(symbol, start, end)
  if (!prices.length) {
    throw new Error(`标的 ${symbol} 没有价格数据`)
  }

  const x = prices.map((p) => p.ts)
  const cols = [toColumn('价格', prices.map((p) => p.close))]

  // 买卖信号打到对应标的价格上
  const signals = [
    { key: 'buy', label: '买入', list: result?.buy ?? [], color: 'good' },
    { key: 'sell', label: '卖出', list: result?.sell ?? [], color: 'bad' },
  ] as const

  const markers: Array<{ x: number; series: string; kind: string; label: string }> = []
  for (const sig of signals) {
    const values = new Array(prices.length).fill(null)
    for (const s of sig.list) {
      if (s[0] !== symbol) continue
      const idx = nearestIndex(x, s[1])
      if (idx < 0) continue
      values[idx] = prices[idx].close
      markers.push({ x: idx, series: sig.label, kind: sig.key, label: `${sig.label} @ ${shortDate(s[1])}` })
    }
    if (values.some((v) => v !== null)) {
      cols.push({ ...toColumn(sig.label, values), tone: sig.color === 'good' ? 'good' : 'bad' })
    }
  }

  return {
    data: {
      x,
      cols,
      markers,
      x_label: '日期',
      y_label: '价格',
      y_format: 'number',
    },
  }
})

/** 找到最接近的时间戳下标 */
function nearestIndex(timestamps: number[], ts: number): number {
  if (!timestamps.length) return -1
  let best = -1
  let bestDiff = Infinity
  for (let i = 0; i < timestamps.length; i++) {
    const diff = Math.abs(timestamps[i] - ts)
    if (diff < bestDiff) {
      bestDiff = diff
      best = i
    }
  }
  // 容忍一天内的偏差
  return bestDiff <= 86400 ? best : -1
}

/**
 * 基准自身的累计收益曲线。
 *
 * args:
 *   - benchmark?  基准代码
 *   - version_id? 用回测时间范围
 */registerResolver('benchmark_curve', async (args) => {
  const benchmark = (args.benchmark as string) || DEFAULT_BENCHMARK
  const history = useHistoryStore()
  const versionId = args.version_id as string | undefined
  const result = versionId
    ? await history.loadBacktestResult(versionId)
    : await history.getLatestBacktestResult()

  const { start, end } = result ? inferRange(result) : {
    start: new Date(Date.now() - 365 * 86400000),
    end: new Date(),
  }

  const bench = await getBenchmark(benchmark, start, end)
  if (!bench.data?.length) throw new Error(`基准 ${benchmark} 没有数据`)

  const returns = bench.data.map((d, i) => {
    const prev = bench.data[i - 1]
    return prev ? (d.close - prev.close) / prev.close : 0
  })

  return {
    data: {
      x: bench.data.map((d) => Math.floor(d.time / 1000)),
      cols: [toColumn(`基准收益(${bench.name || benchmark})`, toCumulative(returns))],
      x_label: '日期',
      y_label: '累计收益',
      y_format: 'percent',
    },
    controls: [
      {
        key: 'benchmark',
        label: '基准',
        kind: 'select',
        layer: 'data',
        options: BENCHMARK_OPTIONS,
        default: benchmark,
      },
    ],
  }
})

/**
 * 蒙特卡洛路径。
 *
 * args:
 *   - version_id? 指定版本；缺省用最近一次回测
 */
registerResolver('mc_paths', async (args) => {
  const history = useHistoryStore()
  const versionId = args.version_id as string | undefined
  const result = versionId
    ? await history.loadBacktestResult(versionId)
    : await history.getLatestBacktestResult()

  if (!result) {
    throw new Error('没有可用的回测结果，请先执行一次回测')
  }

  const paths = (result as any).mcPaths
  if (!paths?.worst?.length && !paths?.best?.length) {
    throw new Error('这次回测没有蒙特卡洛路径数据（后端未返回 mc_paths）')
  }

  const { timestamps } = extractDailySeries(result)

  return {
    data: {
      worst: paths.worst ?? [],
      best: paths.best ?? [],
      median: paths.median,
      p10: paths.p10,
      p90: paths.p90,
      bar_count: timestamps.length || 252,
    },
  }
})
