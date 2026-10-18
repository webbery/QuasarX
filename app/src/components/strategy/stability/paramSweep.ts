// app/src/components/strategy/stability/paramSweep.ts
// 稳定性 C 组：参数扰动 —— 执行逻辑
//
// 做法：跑一次基准，再对每个被选中的数值型参数做对称网格扰动，一次只动一个参数，
// 其余全部锁死为基准值（正交，否则无法归因）。
//
// 注意每次运行都要唯一 id：后端按 script.id 从 CapitalPool 分配资金并注册回测上下文。

import { runBacktestOnce } from './stabilityApi'
import {
  buildGrid,
  judgeParamEntry,
  aggregateParamSweep,
  type ParamSweepEntry,
  type ParamBaseline,
  type PerturbableParam,
  type ParamSweepAnalysis,
} from './paramSweepStats'

export interface ParamSweepResult {
  baseCapital: number
  gridSize: number
  entries: ParamSweepEntry[]
  analysis: ParamSweepAnalysis
}

const num = (features: Record<string, number> | undefined, key: string): number | null => {
  const v = features?.[key]
  return typeof v === 'number' && Number.isFinite(v) ? v : null
}

const toBaseline = (res: {
  features?: Record<string, number>
  summary?: { buy_count?: number; sell_count?: number }
}): ParamBaseline => ({
  annualReturn: num(res.features, 'annual_return'),
  totalReturn: num(res.features, 'total_return'),
  sharp: num(res.features, 'sharp'),
  maxDrawdown: num(res.features, 'max_drawdown'),
  tradeCount: Number(res.summary?.buy_count ?? 0) + Number(res.summary?.sell_count ?? 0),
})

export interface ParamSweepOptions {
  baseGraph: Record<string, any>
  baseCapital: number
  /** 用户勾选要扫描的参数 */
  selectedParams: PerturbableParam[]
  gridSize: number
  runInterval: number
  onProgress?: (done: number, total: number, label: string) => void
  shouldAbort?: () => boolean
}

export async function runParamSweep(opts: ParamSweepOptions): Promise<ParamSweepResult> {
  const { baseGraph, baseCapital, selectedParams, gridSize, runInterval, onProgress, shouldAbort } = opts

  const stamp = Date.now().toString(36)
  const baseId = String(baseGraph.id ?? 'strategy').replace(/[^A-Za-z0-9_]/g, '')

  const buildScript = (tag: string, mutate?: (script: Record<string, any>) => void): Record<string, any> => {
    const script = JSON.parse(JSON.stringify(baseGraph))
    script.id = `stab_${baseId}_${tag}_${stamp}`
    script.capital = baseCapital
    mutate?.(script)
    return script
  }

  const total = 1 + selectedParams.reduce((sum, p) => sum + buildGrid(p, gridSize).length, 0)

  // 基准运行：所有参数共用同一个 k=0 点
  const baselineRes = await runBacktestOnce(JSON.stringify(buildScript('pbase')))
  if (baselineRes.status === 'error' || !baselineRes.features) {
    throw new Error(baselineRes.message || '基准回测失败，无法进行参数扰动')
  }
  const baseline = toBaseline(baselineRes)
  onProgress?.(1, total, '基准')

  const entries: ParamSweepEntry[] = []
  let done = 1

  for (const param of selectedParams) {
    if (shouldAbort?.()) break
    const grid = buildGrid(param, gridSize)
    const variants: Array<{
      offset: number
      value: number
      ok: boolean
      error?: string
      annualReturn: number | null
      totalReturn: number | null
      sharp: number | null
      maxDrawdown: number | null
      tradeCount: number
    }> = []

    for (let i = 0; i < grid.length; i++) {
      if (shouldAbort?.()) break
      const v = grid[i]
      let variant
      try {
        const res = await runBacktestOnce(
          JSON.stringify(
            buildScript(`p${entries.length}_${i}`, script => {
              const target = script.nodes?.find((n: any) => n.id === param.nodeId)
              if (target?.data?.params?.[param.key]) {
                target.data.params[param.key].value = v.value
              }
            })
          )
        )
        if (res.status === 'error' || !res.features) {
          throw new Error(res.message || '回测未返回指标')
        }
        variant = {
          offset: v.offset,
          value: v.value,
          ok: true,
          ...toBaseline(res),
        }
      } catch (err: any) {
        variant = {
          offset: v.offset,
          value: v.value,
          ok: false,
          error: err?.response?.data?.message || err?.message || String(err),
          annualReturn: null,
          totalReturn: null,
          sharp: null,
          maxDrawdown: null,
          tradeCount: 0,
        }
      }
      variants.push(variant)
      done++
      onProgress?.(done, total, `${param.nodeLabel}/${param.label} → ${v.value}`)
      if (runInterval > 0 && i < grid.length - 1) {
        await new Promise(resolve => setTimeout(resolve, runInterval))
      }
    }

    entries.push(judgeParamEntry(param, baseline, variants))
  }

  onProgress?.(total, total, '完成')

  return {
    baseCapital,
    gridSize,
    entries,
    analysis: aggregateParamSweep(entries),
  }
}
