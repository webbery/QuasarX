/**
 * 抽取健康度与熔断
 *
 * 连续失败达到阈值后打开熔断，熔断期间直接跳过抽取，
 * 不调用模型、不写库；冷却结束后放一次探测。
 *
 * 解析类失败不计入连续失败：解析失败说明端点是活的。
 */

import type { ErrorCategory } from './extraction'

export type { ErrorCategory } from './extraction'

/** 熔断状态 */
export type BreakerState = 'closed' | 'open' | 'halfOpen'

/** 健康度快照 */
export interface ExtractionHealth {
  /** 连续失败次数 */
  consecutiveFailures: number
  /** 熔断状态 */
  breakerState: BreakerState
  /** 熔断打开的时间戳 */
  openedAt: number | null
  /** 最近一次错误信息 */
  lastError: string | null
  /** 最近一次错误分类 */
  lastErrorCategory: ErrorCategory | null
}

/** 连续失败多少次后打开熔断 */
export const FAILURE_THRESHOLD = 5

/** 首次冷却时长 */
export const BASE_COOLDOWN_MS = 60_000

/** 冷却时长上限 */
export const MAX_COOLDOWN_MS = 600_000

let state: ExtractionHealth = {
  consecutiveFailures: 0,
  breakerState: 'closed',
  openedAt: null,
  lastError: null,
  lastErrorCategory: null,
}

let cooldownMs = BASE_COOLDOWN_MS
let probeUsed = false

/** 记录一次成功 */
export function recordSuccess(): void {
  state = {
    consecutiveFailures: 0,
    breakerState: 'closed',
    openedAt: null,
    lastError: null,
    lastErrorCategory: null,
  }
  cooldownMs = BASE_COOLDOWN_MS
  probeUsed = false
}

/** 记录一次失败 */
export function recordFailure(category: ErrorCategory, message?: string): void {
  if (category === 'parse' || category === 'schema') {
    state = { ...state, lastError: message ?? null, lastErrorCategory: category }
    return
  }

  const consecutiveFailures = state.consecutiveFailures + 1
  const now = Date.now()

  if (state.breakerState === 'halfOpen') {
    cooldownMs = Math.min(cooldownMs * 2, MAX_COOLDOWN_MS)
    state = {
      consecutiveFailures,
      breakerState: 'open',
      openedAt: now,
      lastError: message ?? null,
      lastErrorCategory: category,
    }
    probeUsed = false
    return
  }

  if (consecutiveFailures >= FAILURE_THRESHOLD) {
    state = {
      consecutiveFailures,
      breakerState: 'open',
      openedAt: now,
      lastError: message ?? null,
      lastErrorCategory: category,
    }
    probeUsed = false
    return
  }

  state = {
    ...state,
    consecutiveFailures,
    lastError: message ?? null,
    lastErrorCategory: category,
  }
}

/**
 * 当前是否允许发起抽取
 *
 * 熔断打开且冷却未结束时返回 false；冷却结束后转 halfOpen，放一次探测。
 */
export function isExtractionAllowed(now: number = Date.now()): boolean {
  if (state.breakerState === 'closed') return true

  if (state.breakerState === 'open') {
    const openedAt = state.openedAt ?? 0
    if (now - openedAt < cooldownMs) return false
    state = { ...state, breakerState: 'halfOpen' }
    probeUsed = false
  }

  if (probeUsed) return false
  probeUsed = true
  return true
}

/** 健康度快照 */
export function getExtractionHealth(): ExtractionHealth {
  return { ...state }
}

/** 重置（测试与手动恢复用） */
export function resetExtractionHealth(): void {
  state = {
    consecutiveFailures: 0,
    breakerState: 'closed',
    openedAt: null,
    lastError: null,
    lastErrorCategory: null,
  }
  cooldownMs = BASE_COOLDOWN_MS
  probeUsed = false
}
