// app/src/components/strategy/stability/capitalSweep.ts
// 稳定性 A 组：资金规模不变性 —— 执行逻辑
//
// 检验逻辑：同一张策略图，只改 script.capital 循环回测，看收益能力是否随资金规模变化。
//
// 预期形状由执行节点的滑点模型决定（SlippageModel.cpp）：
//   fixed_ratio   slip = price × ratio              —— 与下单量无关，R(c) 应平坦
//   volume_impact slip = price × (base + k×(vol ratio)^α) —— vol ratio ∝ 本金，
//                                                      R(c) 应随 c 单调递减
// 两个模型都未启用时 R(c) 必然平坦，那是「没建模」而不是「容量大」，结论只能是「未检验」。

import { fetchCapitalPool, runBacktestOnce } from './stabilityApi'

/** 标准资金档位，按资金池上限过滤 */
const STANDARD_TIERS = [100_000, 300_000, 1_000_000, 3_000_000, 10_000_000]

export interface SlippageConfig {
  /** 0 = 固定比例，1 = 成交量冲击 */
  modelType: number
  fixedRatio: number
  base: number
  impactK: number
  alpha: number
  /** 最低手续费（元），绝对额成本，小资金端单笔金额小 → 占比高 */
  minFee: number
  found: boolean
}

/** 滑点/冲击成本是否已启用 —— 决定扫描结果能不能作为容量证据 */
export function isSlippageModeled(cfg: SlippageConfig): boolean {
  if (!cfg.found) return false
  return cfg.modelType === 1 || (cfg.modelType === 0 && cfg.fixedRatio > 0)
}

export function readSlippageConfig(graph: { nodes?: any[] } | null): SlippageConfig {
  const empty: SlippageConfig = {
    modelType: 0,
    fixedRatio: 0,
    base: 0.001,
    impactK: 0.01,
    alpha: 0.5,
    minFee: 5,
    found: false,
  }
  if (!graph?.nodes) return empty

  const num = (v: unknown, fallback: number): number => {
    const n = typeof v === 'object' && v !== null ? (v as any).value : v
    const parsed = Number(n)
    return Number.isFinite(parsed) ? parsed : fallback
  }

  for (const node of graph.nodes) {
    if (node?.data?.nodeType !== 'execution') continue
    const params = node.data.params ?? {}
    return {
      modelType: num(params.slippageModel?.value, 0),
      fixedRatio: num(params.slippage?.value, 0),
      base: num(params.slippageBase?.value, 0.001),
      impactK: num(params.slippageImpactK?.value, 0.01),
      alpha: num(params.slippageAlpha?.value, 0.5),
      minFee: num(params.minFee?.value, 5),
      found: true,
    }
  }
  return empty
}

/** 按资金池上限生成档位；标准档位不足 2 个时退化为按上限比例生成 */
export function generateCapitalTiers(cap: number): number[] {
  const usable = STANDARD_TIERS.filter(t => t <= cap)
  if (usable.length >= 2) return usable
  return [cap * 0.25, cap * 0.5, cap]
}

export interface CapitalRun {
  capital: number
  ok: boolean
  error?: string
  totalReturn: number | null
  annualReturn: number | null
  sharp: number | null
  maxDrawdown: number | null
  tradeCount: number
  /** 成交金额 / 本金 */
  turnover: number | null
}

export interface CapitalSweepResult {
  slippage: SlippageConfig
  slippageModeled: boolean
  baselineCapital: number
  runs: CapitalRun[]
  /** 资金敏感性 ε = (max R − min R) / |median R| */
  epsilon: number | null
  /** R(c) / R(c_base) */
  normalized: Array<{ capital: number; ratio: number | null }>
  /** 笔数比 N(c)/N(c_base)，理论应与 c/c_base 同比例 */
  tradeRatio: Array<{ capital: number; ratio: number | null }>
  verdict: {
    level: 'green' | 'yellow' | 'red' | 'unchecked'
    text: string
  }
}

const num = (features: Record<string, number> | undefined, key: string): number | null => {
  const v = features?.[key]
  return typeof v === 'number' && Number.isFinite(v) ? v : null
}

/** 成交金额合计（buy/sell 均为 [symbol, timestamp, quantity, price]） */
function tradedAmount(trades: [string, number, number, number][] | undefined): number {
  if (!trades) return 0
  return trades.reduce((sum, t) => sum + (t[2] ?? 0) * (t[3] ?? 0), 0)
}

/** 序列是否整体单调不增（允许 tolerance 的噪声） */
function isMonotoneDecreasing(values: number[]): boolean {
  if (values.length < 3) return true
  let nonIncreasing = 0
  for (let i = 1; i < values.length; i++) {
    if (values[i] <= values[i - 1] + 0.005) nonIncreasing++
  }
  return nonIncreasing / (values.length - 1) >= 0.7
}

function median(values: number[]): number {
  const sorted = [...values].sort((a, b) => a - b)
  const mid = Math.floor(sorted.length / 2)
  return sorted.length % 2 ? sorted[mid] : (sorted[mid - 1] + sorted[mid]) / 2
}

export function judgeCapitalSweep(
  slippage: SlippageConfig,
  modeled: boolean,
  runs: CapitalRun[],
  epsilon: number | null
): CapitalSweepResult['verdict'] {
  const returns = runs.filter(r => r.ok && r.totalReturn !== null)
  if (returns.length < 2) {
    return { level: 'unchecked', text: '成功档位不足 2 个，无法判定资金规模不变性' }
  }

  if (!modeled) {
    return {
      level: 'unchecked',
      text:
        '执行节点未启用滑点/冲击成本（固定比例模型且固定滑点为 0），收益曲线平坦只说明成本未建模，容量未检验。' +
        '请在执行节点将滑点模型切到「成交量冲击」后重跑。',
    }
  }

  const values = returns
    .slice()
    .sort((a, b) => a.capital - b.capital)
    .map(r => r.totalReturn as number)
  if (values.some(v => v <= 0)) {
    return { level: 'red', text: '存在资金档位总收益率转负 —— 规模放大到该档即失去获取收益的能力。' }
  }

  if (slippage.modelType === 1) {
    const monotone = isMonotoneDecreasing(values)
    // 容量参照系取最小资金档（冲击影响最小的那一档），不能用「最接近用户当前配置的档位」——
    // 那会让结论随用户当前本金漂移
    const reference = values[0]
    const topReturn = values[values.length - 1]
    const retention = reference > 0 ? topReturn / reference : 1
    if (!monotone) {
      return {
        level: 'yellow',
        text: '成交量冲击模型下收益本应随资金单调递减，实际存在非单调档位 —— 请检查是否有资金无关的绝对额成本。',
      }
    }
    if (reference > 0 && retention < 0.5) {
      return {
        level: 'yellow',
        text: `收益随资金单调递减符合冲击成本预期，但最高档只剩最小资金档收益的 ${(retention * 100).toFixed(0)}%（容量受限）。`,
      }
    }
    return {
      level: 'green',
      text: `收益随资金单调递减、且各档均为正，符合成交量冲击模型的预期形状（冲击项 ∝ c^${slippage.alpha}），最高档保留最小档收益的 ${(retention * 100).toFixed(0)}%。`,
    }
  }

  // fixed_ratio：预期平坦
  if (epsilon !== null && epsilon >= 0.3) {
    return {
      level: 'yellow',
      text: `固定比例滑点模型下收益本应与资金无关，实际资金敏感性 ε=${epsilon.toFixed(3)} 偏高 —— 存在绝对额成本或权重上限。`,
    }
  }
  return {
    level: 'green',
    text: `固定比例滑点模型下收益与资金规模基本无关（ε=${epsilon?.toFixed(3) ?? '-'}），符合预期。`,
  }
}

export interface CapitalSweepOptions {
  /** 基准策略图（已做过 convertLabelsToKeys + normalizeCodeParams） */
  baseGraph: Record<string, any>
  /** 用户当前配置的本金，用于选归一化基准档 */
  configuredCapital: number
  /** 运行间隔 ms，避免连续请求打爆服务 */
  runInterval: number
  onProgress?: (done: number, total: number, capital: number) => void
  shouldAbort?: () => boolean
}

export async function runCapitalSweep(opts: CapitalSweepOptions): Promise<CapitalSweepResult> {
  const { baseGraph, configuredCapital, runInterval, onProgress, shouldAbort } = opts

  const pool = await fetchCapitalPool()
  const tiers = generateCapitalTiers(pool.totalAvailable)

  const slippage = readSlippageConfig(baseGraph)
  const modeled = isSlippageModeled(slippage)

  const stamp = Date.now().toString(36)
  const baseId = String(baseGraph.id ?? 'strategy').replace(/[^A-Za-z0-9_]/g, '')

  const runs: CapitalRun[] = []
  for (let i = 0; i < tiers.length; i++) {
    if (shouldAbort?.()) break
    const capital = tiers[i]
    onProgress?.(i, tiers.length, capital)

    // 每次运行必须用唯一 id：后端按 script.id 从 CapitalPool 分配资金并注册回测上下文，
    // 同名连发会互相踩。唯一命名还有个附带好处——reclaim 只置 active=false 不清零，
    // 被 reclaim 过的策略不再计入 totalAvailable，扫描不会把资金池越跑越少。
    const script = JSON.parse(JSON.stringify(baseGraph))
    script.id = `stab_${baseId}_${i}_${stamp}`
    // 必须显式传数字：不传 capital 后端会按 totalAvailable/activeCount 自动分配，
    // 而 activeCount 随当时活跃策略数漂移，基准本身就不可复现
    script.capital = capital

    let run: CapitalRun
    try {
      const res = await runBacktestOnce(JSON.stringify(script))
      if (res.status === 'error' || !res.features) {
        throw new Error(res.message || '回测未返回指标')
      }
      const amount = tradedAmount(res.buy) + tradedAmount(res.sell)
      const tradeCount = Number(res.summary?.buy_count ?? 0) + Number(res.summary?.sell_count ?? 0)
      run = {
        capital,
        ok: true,
        totalReturn: num(res.features, 'total_return'),
        annualReturn: num(res.features, 'annual_return'),
        sharp: num(res.features, 'sharp'),
        maxDrawdown: num(res.features, 'max_drawdown'),
        tradeCount,
        turnover: capital > 0 ? amount / capital : null,
      }
    } catch (err: any) {
      run = {
        capital,
        ok: false,
        error: err?.response?.data?.message || err?.message || String(err),
        totalReturn: null,
        annualReturn: null,
        sharp: null,
        maxDrawdown: null,
        tradeCount: 0,
        turnover: null,
      }
    }
    runs.push(run)

    if (runInterval > 0 && i < tiers.length - 1) {
      await new Promise(resolve => setTimeout(resolve, runInterval))
    }
  }
  onProgress?.(tiers.length, tiers.length, tiers[tiers.length - 1] ?? 0)

  const okRuns = runs.filter(r => r.ok)
  const returns = okRuns.map(r => r.totalReturn).filter((v): v is number => v !== null)

  let epsilon: number | null = null
  if (returns.length >= 2) {
    const denom = Math.abs(median(returns))
    epsilon = denom > 1e-12 ? (Math.max(...returns) - Math.min(...returns)) / denom : null
  }

  // 归一化基准取最接近用户当前配置本金的档位
  const baselineCapital =
    okRuns.reduce<CapitalRun | undefined>(
      (best, r) =>
        best === undefined || Math.abs(r.capital - configuredCapital) < Math.abs(best.capital - configuredCapital)
          ? r
          : best,
      undefined
    )?.capital ?? tiers[0]

  const baselineRun = runs.find(r => r.capital === baselineCapital)
  const baseReturn = baselineRun?.totalReturn ?? null
  const baseTrades = baselineRun?.tradeCount ?? null

  return {
    slippage,
    slippageModeled: modeled,
    baselineCapital,
    runs,
    epsilon,
    normalized: runs
      .filter(r => r.ok)
      .map(r => ({
        capital: r.capital,
        ratio: baseReturn !== null && baseReturn !== 0 && r.totalReturn !== null
          ? r.totalReturn / baseReturn
          : null,
      })),
    tradeRatio: runs
      .filter(r => r.ok)
      .map(r => ({
        capital: r.capital,
        ratio: baseTrades ? r.tradeCount / baseTrades : null,
      })),
    verdict: judgeCapitalSweep(slippage, modeled, runs, epsilon),
  }
}
