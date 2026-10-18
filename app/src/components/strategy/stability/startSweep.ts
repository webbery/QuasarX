// app/src/components/strategy/stability/startSweep.ts
// 稳定性 B 组：回测起点不变性 —— 执行逻辑
//
// 做法：先跑一次基准回测拿到 daily_dates，按固定步长取样得到一串候选起点日期，
// 再逐个把 script.backtest.start 右移跑一遍，end 固定不变、capital 锁死为基准档。
//
// 比较一律用后端算好的 annual_return，而不是 total_return —— end 固定时样本长度递减，
// 用总收益会伪造出「起点越晚越差」的假象。

import { runBacktestOnce } from './stabilityApi'
import { analyzeStartSensitivity, type StartSensitivityAnalysis } from './startSweepStats'

export interface StartPointRun {
  startDate: string
  ok: boolean
  error?: string
  annualReturn: number | null
  totalReturn: number | null
  sharp: number | null
  maxDrawdown: number | null
  tradeCount: number
}

export interface StartSweepResult {
  baseCapital: number
  endDate: string | null
  stepBars: number
  tailGuardBars: number
  runs: StartPointRun[]
  analysis: StartSensitivityAnalysis
}

/** epoch 秒 → 本地日期字符串。必须用本地时区：UTC 会整体偏一天 */
function epochToDateString(epochSeconds: number): string {
  const d = new Date(epochSeconds * 1000)
  const y = d.getFullYear()
  const m = String(d.getMonth() + 1).padStart(2, '0')
  const day = String(d.getDate()).padStart(2, '0')
  return `${y}-${m}-${day}`
}

const num = (features: Record<string, number> | undefined, key: string): number | null => {
  const v = features?.[key]
  return typeof v === 'number' && Number.isFinite(v) ? v : null
}

export interface StartSweepOptions {
  baseGraph: Record<string, any>
  /** 锁死的基准本金 */
  baseCapital: number
  /** 起点偏移步长，单位 bar */
  stepBars: number
  /** 末尾保留的 bar 数，这段起点不参与判定（样本过短不可比） */
  tailGuardBars: number
  runInterval: number
  onProgress?: (done: number, total: number, startDate: string) => void
  shouldAbort?: () => boolean
}

export async function runStartSweep(opts: StartSweepOptions): Promise<StartSweepResult> {
  const { baseGraph, baseCapital, stepBars, tailGuardBars, runInterval, onProgress, shouldAbort } = opts

  const stamp = Date.now().toString(36)
  const baseId = String(baseGraph.id ?? 'strategy').replace(/[^A-Za-z0-9_]/g, '')
  const endDate: string | null = baseGraph.backtest?.end ?? null

  const buildScript = (startDate: string, tag: string): Record<string, any> => {
    const script = JSON.parse(JSON.stringify(baseGraph))
    script.id = `stab_${baseId}_${tag}_${stamp}`
    script.capital = baseCapital
    if (startDate) {
      script.backtest = { ...(script.backtest ?? {}), start: startDate }
    }
    return script
  }

  // 基准运行：既拿到第 0 个起点（不偏移）的结果，也拿到交易日历
  const baseline = await runBacktestOnce(JSON.stringify(buildScript('', 'base')))
  if (baseline.status === 'error' || !baseline.features) {
    throw new Error(baseline.message || '基准回测失败，无法生成起点序列')
  }

  const dailyDates = Array.isArray(baseline.daily_dates) ? (baseline.daily_dates as number[]) : []
  if (dailyDates.length < 4) {
    throw new Error('基准回测未返回足够的交易日序列，无法生成起点序列')
  }

  const originalStart: string | null = baseGraph.backtest?.start ?? null
  const baselineRun: StartPointRun = {
    startDate: originalStart ?? epochToDateString(dailyDates[0]),
    ok: true,
    annualReturn: num(baseline.features, 'annual_return'),
    totalReturn: num(baseline.features, 'total_return'),
    sharp: num(baseline.features, 'sharp'),
    maxDrawdown: num(baseline.features, 'max_drawdown'),
    tradeCount: Number(baseline.summary?.buy_count ?? 0) + Number(baseline.summary?.sell_count ?? 0),
  }

  // 候选起点：尾部保留 tailGuardBars，保证每段样本不至于短到不可比
  const lastIndex = Math.max(0, dailyDates.length - tailGuardBars - 1)
  const candidates: string[] = []
  for (let i = stepBars; i <= lastIndex; i += stepBars) {
    candidates.push(epochToDateString(dailyDates[i]))
  }

  const runs: StartPointRun[] = [baselineRun]
  const total = candidates.length + 1
  onProgress?.(1, total, baselineRun.startDate)

  for (let i = 0; i < candidates.length; i++) {
    if (shouldAbort?.()) break
    const startDate = candidates[i]
    let run: StartPointRun
    try {
      const res = await runBacktestOnce(JSON.stringify(buildScript(startDate, `s${i}`)))
      if (res.status === 'error' || !res.features) {
        throw new Error(res.message || '回测未返回指标')
      }
      run = {
        startDate,
        ok: true,
        annualReturn: num(res.features, 'annual_return'),
        totalReturn: num(res.features, 'total_return'),
        sharp: num(res.features, 'sharp'),
        maxDrawdown: num(res.features, 'max_drawdown'),
        tradeCount: Number(res.summary?.buy_count ?? 0) + Number(res.summary?.sell_count ?? 0),
      }
    } catch (err: any) {
      run = {
        startDate,
        ok: false,
        error: err?.response?.data?.message || err?.message || String(err),
        annualReturn: null,
        totalReturn: null,
        sharp: null,
        maxDrawdown: null,
        tradeCount: 0,
      }
    }
    runs.push(run)
    onProgress?.(i + 2, total, startDate)

    if (runInterval > 0 && i < candidates.length - 1) {
      await new Promise(resolve => setTimeout(resolve, runInterval))
    }
  }

  const analysis = analyzeStartSensitivity(runs.map(r => r.annualReturn).filter((v): v is number => v !== null))

  return {
    baseCapital,
    endDate,
    stepBars,
    tailGuardBars,
    runs,
    analysis,
  }
}
