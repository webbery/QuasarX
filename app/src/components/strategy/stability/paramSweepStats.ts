// app/src/components/strategy/stability/paramSweepStats.ts
// 稳定性 C 组：参数扰动 —— 网格生成与判定
//
// 核心问题：「参数稍微一动就崩」说明收益是拟合出来的而不是结构性的。
// 做法：对每个数值型参数做对称网格扰动，一次只动一个（正交），其余锁死为基准，
// 看邻域内的收益分布。
//
// 关键指标：
//   positiveRatio     邻域正收益占比（越高越稳）
//   baselineIsolation (R(基准) − max(R(扰动))) / |R(基准)|
//                      基准远高于任何扰动 → 参数处在刃口，是过拟合的典型形态
//
// 全部为纯函数，不依赖后端，可单测。

export interface PerturbableParam {
  nodeId: string
  nodeLabel: string
  nodeType: string
  key: string
  label: string
  value: number
  /** 扰动步长：优先取参数自带的 step，缺失时退化为 value × delta */
  step: number
  min?: number
  max?: number
}

/**
 * 从策略图提取可扰动的数值型参数。
 * 只取 params[key].type === 'number' —— select/boolean 的值是枚举，
 * 扰动 0.5 没有意义；text/date 类本就不是数值。
 */
export function extractPerturbableParams(
  graph: { nodes?: any[] } | null,
  fallbackDelta = 0.1
): PerturbableParam[] {
  if (!graph?.nodes) return []
  const out: PerturbableParam[] = []

  for (const node of graph.nodes) {
    const data = node?.data
    if (!data?.nodeType) continue
    const params = data.params
    if (!params || typeof params !== 'object') continue

    for (const [key, cfg] of Object.entries(params as Record<string, any>)) {
      if (!cfg || typeof cfg !== 'object') continue
      if (cfg.type !== 'number') continue
      const value = Number(cfg.value)
      if (!Number.isFinite(value)) continue

      const rawStep = Number(cfg.step)
      const step = Number.isFinite(rawStep) && rawStep > 0 ? rawStep : Math.max(Math.abs(value) * fallbackDelta, 1e-6)

      out.push({
        nodeId: String(node.id ?? ''),
        nodeLabel: String(data.label ?? data.nodeType),
        nodeType: String(data.nodeType),
        key,
        label: String(cfg.label ?? key),
        value,
        step,
        min: Number.isFinite(Number(cfg.min)) ? Number(cfg.min) : undefined,
        max: Number.isFinite(Number(cfg.max)) ? Number(cfg.max) : undefined,
      })
    }
  }
  return out
}

export interface ParamVariant {
  /** 相对基准的档位偏移，-half..-1 / 1..half，不含 0 */
  offset: number
  value: number
}

/**
 * 生成对称网格（不含 k=0，基准值单独跑一次）。
 * 按 min/max 夹紧；夹紧后可能多个 offset 落到同一数值，去重。
 */
export function buildGrid(param: PerturbableParam, gridSize: number): ParamVariant[] {
  const size = Math.max(3, gridSize % 2 === 0 ? gridSize + 1 : gridSize)
  const half = Math.floor((size - 1) / 2)

  const clamp = (v: number): number => {
    let out = v
    if (param.min !== undefined && out < param.min) out = param.min
    if (param.max !== undefined && out > param.max) out = param.max
    return out
  }

  const seen = new Set<number>([param.value])
  const variants: ParamVariant[] = []
  for (let k = -half; k <= half; k++) {
    if (k === 0) continue
    const value = clamp(param.value + k * param.step)
    if (seen.has(value)) continue
    seen.add(value)
    variants.push({ offset: k, value })
  }
  return variants
}

export interface ParamVariantRun {
  offset: number
  value: number
  ok: boolean
  error?: string
  annualReturn: number | null
  totalReturn: number | null
  sharp: number | null
  maxDrawdown: number | null
  tradeCount: number
}

export interface ParamBaseline {
  annualReturn: number | null
  totalReturn: number | null
  sharp: number | null
  maxDrawdown: number | null
  tradeCount: number
}

export interface ParamSweepEntry {
  param: PerturbableParam
  baseline: ParamBaseline
  variants: ParamVariantRun[]
  stats: {
    /** 邻域正收益占比 */
    positiveRatio: number | null
    /** (R(基准) − max(R(扰动))) / |R(基准)|，越大说明参数越处在刃口 */
    baselineIsolation: number | null
    worstVariant: ParamVariantRun | null
    bestVariant: ParamVariantRun | null
  }
  verdict: { level: 'green' | 'yellow' | 'red'; text: string }
}

export type ParamSweepLevel = 'green' | 'yellow' | 'red'

const LEVEL_ORDER: Record<ParamSweepLevel, number> = { red: 2, yellow: 1, green: 0 }

export function judgeParamEntry(
  param: PerturbableParam,
  baseline: ParamBaseline,
  variants: ParamVariantRun[]
): ParamSweepEntry {
  const okVariants = variants.filter(v => v.ok && v.annualReturn !== null)
  const returns = okVariants.map(v => v.annualReturn as number)

  const positiveRatio = okVariants.length ? okVariants.filter(v => (v.annualReturn as number) > 0).length / okVariants.length : null

  let baselineIsolation: number | null = null
  if (returns.length && baseline.annualReturn !== null && Math.abs(baseline.annualReturn) > 1e-12) {
    baselineIsolation = (baseline.annualReturn - Math.max(...returns)) / Math.abs(baseline.annualReturn)
  }

  const worstVariant = okVariants.length
    ? okVariants.reduce((a, b) => ((a.annualReturn as number) <= (b.annualReturn as number) ? a : b))
    : null
  const bestVariant = okVariants.length
    ? okVariants.reduce((a, b) => ((a.annualReturn as number) >= (b.annualReturn as number) ? a : b))
    : null

  let level: ParamSweepLevel = 'green'
  let text = ''
  if (!okVariants.length) {
    level = 'red'
    text = '所有扰动档位均运行失败，无法判定该参数的稳健性'
  } else if (positiveRatio !== null && positiveRatio < 0.5) {
    level = 'red'
    text = `邻域仅 ${(positiveRatio * 100).toFixed(0)}% 的档位保持正年化收益 —— 参数一动就崩，典型的过拟合形态`
  } else if (positiveRatio !== null && positiveRatio < 0.8) {
    level = 'yellow'
    text = `邻域正收益占比 ${(positiveRatio * 100).toFixed(0)}%，最差档位年化 ${((worstVariant?.annualReturn ?? 0) * 100).toFixed(2)}%`
  } else if (baselineIsolation !== null && baselineIsolation > 0.5) {
    level = 'yellow'
    text = `邻域正收益占比 ${((positiveRatio ?? 0) * 100).toFixed(0)}%，但基准值比任何扰动档位高出 ${(baselineIsolation * 100).toFixed(0)}% —— 参数处在刃口`
  } else {
    text = `邻域正收益占比 ${((positiveRatio ?? 0) * 100).toFixed(0)}%，基准值与扰动档位无显著落差 —— 参数处在平台区`
  }

  return {
    param,
    baseline,
    variants,
    stats: { positiveRatio, baselineIsolation, worstVariant, bestVariant },
    verdict: { level, text },
  }
}

export interface ParamSweepAnalysis {
  entries: ParamSweepEntry[]
  /** 最脆弱的那个参数决定整体判定 */
  verdict: { level: ParamSweepLevel; text: string }
}

export function aggregateParamSweep(entries: ParamSweepEntry[]): ParamSweepAnalysis {
  if (!entries.length) {
    return {
      entries,
      verdict: { level: 'red', text: '没有可扰动的数值型参数' },
    }
  }

  const worst = entries.reduce((a, b) =>
    LEVEL_ORDER[b.verdict.level] > LEVEL_ORDER[a.verdict.level] ? b : a
  )
  const weakest = worst.stats.positiveRatio

  return {
    entries,
    verdict: {
      level: worst.verdict.level,
      text: `共扫描 ${entries.length} 个参数，最脆弱的是「${worst.param.nodeLabel} / ${worst.param.label}」（邻域正收益占比 ${((weakest ?? 0) * 100).toFixed(0)}%）：${worst.verdict.text}`,
    },
  }
}
