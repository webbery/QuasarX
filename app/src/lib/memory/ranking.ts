/**
 * 召回打分（纯函数，无 IO）
 *
 * 打分 = 通道原生分 × 时间衰减因子。
 * 乘法保证语义不匹配的进不来：相似度为 0 时结果必为 0。
 */

import type { MemoryUnit } from './types'

/** 默认时间衰减半衰期：30 天 */
export const DEFAULT_HALF_LIFE_MS = 30 * 24 * 60 * 60 * 1000

/**
 * 时间衰减因子
 *
 * @param occurredAt 事实发生时间（毫秒时间戳）
 * @param now         当前时间（毫秒时间戳）
 * @param halfLifeMs  半衰期（毫秒）；0 表示不衰减
 */
export function decayFactor(occurredAt: number, now: number, halfLifeMs: number): number {
  if (!halfLifeMs) return 1
  if (!Number.isFinite(occurredAt)) return 1
  const age = Math.max(0, now - occurredAt)
  return Math.pow(0.5, age / halfLifeMs)
}

/**
 * 取事实的时效锚点
 *
 * 有区间时取区间终点（事实"最晚仍然成立"的时间），否则取起点。
 */
export function recencyAnchor(unit: MemoryUnit): number {
  if (unit.occurredEnd != null) return unit.occurredEnd
  if (unit.occurredStart != null) return unit.occurredStart
  return unit.mentionedAt
}

/**
 * 单条记忆的最终得分
 *
 * @param baseScore 通道原生分（余弦相似度等）
 * @param unit      记忆单元
 * @param now       当前时间
 * @param halfLifeMs 半衰期；0 表示不衰减
 */
export function scoreMemory(
  baseScore: number,
  unit: MemoryUnit,
  now: number,
  halfLifeMs: number = DEFAULT_HALF_LIFE_MS,
): number {
  return baseScore * decayFactor(recencyAnchor(unit), now, halfLifeMs)
}

/**
 * 按得分降序排序（稳定排序，同分保持原顺序）
 */
export function sortByScore<T>(items: T[], getScore: (t: T) => number): T[] {
  return items
    .map((item, idx) => ({ item, idx, score: getScore(item) }))
    .sort((a, b) => (b.score - a.score) || (a.idx - b.idx))
    .map((x) => x.item)
}
