/**
 * 记忆注入的 token 预算装箱（纯函数，无 IO）
 *
 * 记忆预算与工具结果预算是两套水位：
 * - 工具结果：TOOL_RESULT_MAX_TOKENS / RUN_CONTEXT_BUDGET_TOKENS（src/lib/agent/contextBudget.ts）
 * - 记忆注入：MEMORY_INJECT_TOKENS（本文件）
 */

import { estimateTokens } from '../tokens'
import type { MemoryUnit } from './types'

/** 记忆注入的 token 上限 */
export const MEMORY_INJECT_TOKENS = 1500

/** 装箱参数 */
export interface PackOptions {
  /** token 上限 */
  maxTokens: number
  /** token 估算函数，默认用共享的 estimateTokens */
  estimate?: (text: string) => number
}

/** 装箱结果 */
export interface Packed<T> {
  /** 装入的记忆（按相关性顺序） */
  items: T[]
  /** 实际装入的 token 数 */
  totalTokens: number
  /** 因超预算被丢弃的条数 */
  dropped: number
  /** 是否因超预算发生截断 */
  truncated: boolean
}

/**
 * 按预算贪心装箱：顺序即相关性，填满为止
 *
 * @param items    候选记忆（已按相关性降序）
 * @param getText  取一条记忆的文本
 * @param opts     预算参数
 */
export function packByBudget<T>(
  items: T[],
  getText: (t: T) => string,
  opts: PackOptions,
): Packed<T> {
  const estimate = opts.estimate ?? estimateTokens
  const maxTokens = opts.maxTokens

  const packed: T[] = []
  let totalTokens = 0
  let dropped = 0

  for (const item of items) {
    const tokens = estimate(getText(item))
    if (totalTokens + tokens > maxTokens) {
      dropped++
      continue
    }
    packed.push(item)
    totalTokens += tokens
  }

  return {
    items: packed,
    totalTokens,
    dropped,
    truncated: dropped > 0,
  }
}

/**
 * 把记忆渲染成注入文本
 *
 * @param units   记忆单元（已按相关性降序）
 * @param maxTokens token 上限
 */
export function renderMemoryBlock(units: MemoryUnit[], maxTokens: number): { text: string; packed: Packed<MemoryUnit> } {
  const packed = packByBudget(units, (u) => u.text, { maxTokens })
  const text = packed.items
    .map((u, i) => `[${i + 1}] ${u.text}`)
    .join('\n')
  return { text, packed }
}
