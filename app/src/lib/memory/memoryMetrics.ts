/**
 * 记忆模块的运行指标（纯内存计数）
 *
 * 采集点：抽取调用、抽取失败分类、写入条数、召回耗时、注入 token。
 */

import type { ErrorCategory } from './extraction'
import { getExtractionHealth } from './extractionHealth'

/** 指标快照 */
export interface MemoryMetricsSnapshot {
  /** 抽取调用次数（含重试） */
  extractCalls: number
  /** 抽取成功次数 */
  extractSucceeded: number
  /** 抽取失败次数，按分类 */
  extractFailures: Record<string, number>
  /** schema 不完整但补了默认值的次数 */
  extractDegraded: number
  /** 写入库的记忆条数 */
  retainedUnits: number
  /** 召回次数 */
  recallCalls: number
  /** 注入上下文的 token 累计 */
  injectTokens: number
  /** 最近若干次召回耗时（毫秒） */
  recallLatencies: number[]
  /** 召回耗时 P50 */
  recallP50: number
  /** 召回耗时 P95 */
  recallP95: number
  /** 熔断状态 */
  breakerState: string
}

/** 保留的召回耗时样本数 */
const LATENCY_SAMPLES = 50

const counters = {
  extractCalls: 0,
  extractSucceeded: 0,
  extractFailures: {} as Record<string, number>,
  extractDegraded: 0,
  retainedUnits: 0,
  recallCalls: 0,
  injectTokens: 0,
}

const latencies: number[] = []

/** 记录一次抽取调用 */
export function recordExtractCall(): void {
  counters.extractCalls += 1
}

/** 记录一次抽取成功 */
export function recordExtractSuccess(degraded = false): void {
  counters.extractSucceeded += 1
  if (degraded) counters.extractDegraded += 1
}

/** 记录一次抽取失败 */
export function recordExtractFailure(category: ErrorCategory | string): void {
  const key = String(category)
  counters.extractFailures[key] = (counters.extractFailures[key] ?? 0) + 1
}

/** 记录写入的记忆条数 */
export function recordExtractedFacts(count: number): void {
  counters.retainedUnits += Math.max(0, count)
}

/** 记录一次召回 */
export function recordRecall(tokens: number, latencyMs: number): void {
  counters.recallCalls += 1
  latencies.push(Math.max(0, latencyMs))
  if (latencies.length > LATENCY_SAMPLES) latencies.splice(0, latencies.length - LATENCY_SAMPLES)
}

/** 记录注入的 token 数 */
export function recordInjection(tokens: number): void {
  counters.injectTokens += Math.max(0, tokens)
}

/** 分位数 */
function percentile(samples: number[], p: number): number {
  if (!samples.length) return 0
  const sorted = [...samples].sort((a, b) => a - b)
  const idx = Math.min(sorted.length - 1, Math.max(0, Math.ceil((p / 100) * sorted.length) - 1))
  return sorted[idx]
}

/** 指标快照 */
export function snapshot(): MemoryMetricsSnapshot {
  return {
    extractCalls: counters.extractCalls,
    extractSucceeded: counters.extractSucceeded,
    extractFailures: { ...counters.extractFailures },
    extractDegraded: counters.extractDegraded,
    retainedUnits: counters.retainedUnits,
    recallCalls: counters.recallCalls,
    injectTokens: counters.injectTokens,
    recallLatencies: [...latencies],
    recallP50: percentile(latencies, 50),
    recallP95: percentile(latencies, 95),
    breakerState: getExtractionHealth().breakerState,
  }
}

/** 重置计数 */
export function resetMetrics(): void {
  counters.extractCalls = 0
  counters.extractSucceeded = 0
  counters.extractFailures = {}
  counters.extractDegraded = 0
  counters.retainedUnits = 0
  counters.recallCalls = 0
  counters.injectTokens = 0
  latencies.length = 0
}
