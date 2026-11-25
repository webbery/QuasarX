/**
 * 召回编排：分通道 → 融合 → 打分 → 预算装箱
 */

import type { ChannelHits, ChannelName, FactType, MemoryStore, MemoryUnit } from './types'
import { decayFactor, recencyAnchor, DEFAULT_HALF_LIFE_MS } from './ranking'
import { packByBudget, MEMORY_INJECT_TOKENS } from './budget'

/** RRF 常数 k */
export const RRF_K = 60

/** 召回参数 */
export interface RecallOptions {
  /** bank 作用域 */
  bankId: string
  /** 查询串 */
  query: string
  /** 每通道召回条数 */
  limit?: number
  /** 参与的通道 */
  channels?: ChannelName[]
  /** 限定事实类型 */
  factTypes?: FactType[]
  /** 时间区间过滤 */
  timeRange?: { start: number; end: number }
  /** 注入 token 上限 */
  maxTokens?: number
  /** 时间衰减半衰期；0 表示不衰减 */
  halfLifeMs?: number
  /** 当前时间（注入测试用） */
  now?: number
  /** 融合方式：auto = 多通道走 RRF，单通道用原生分 */
  fusion?: 'auto' | 'rrf' | 'native'
}

/** 召回结果 */
export interface RecallResult {
  /** 装箱后注入的记忆（按相关性降序） */
  units: MemoryUnit[]
  /** 装箱前的完整候选（按相关性降序） */
  candidates: MemoryUnit[]
  /** 各通道的召回明细 */
  hits: ChannelHits[]
  /** 每个候选 id 的最终得分 */
  scores: Map<string, number>
  /** 实际注入的 token 数 */
  totalTokens: number
  /** 因超预算被丢弃的条数 */
  dropped: number
  /** 是否因超预算发生截断 */
  truncated: boolean
}

/**
 * RRF 融合：score = Σ 1/(k + rank)
 *
 * 跨通道融合只吃 rank —— BM25 分数与余弦相似度不可比。
 */
export function rrfFuse(channels: ChannelHits[], k: number = RRF_K): Map<string, number> {
  const scores = new Map<string, number>()
  for (const ch of channels) {
    ch.ids.forEach((id, idx) => {
      const rank = idx + 1
      scores.set(id, (scores.get(id) ?? 0) + 1 / (k + rank))
    })
  }
  return scores
}

/** 取某通道的 id → 原生分映射 */
function nativeScores(channels: ChannelHits[]): Map<string, number> {
  const primary = channels[0]
  const map = new Map<string, number>()
  if (!primary) return map
  primary.ids.forEach((id, idx) => {
    map.set(id, primary.scores?.[idx] ?? 0)
  })
  return map
}

/**
 * 召回
 *
 * @param store 存储适配器
 * @param opts  召回参数
 */
export async function recall(store: MemoryStore, opts: RecallOptions): Promise<RecallResult> {
  const limit = opts.limit ?? 20
  const channels = opts.channels ?? ['vector']
  const now = opts.now ?? Date.now()
  const halfLifeMs = opts.halfLifeMs ?? DEFAULT_HALF_LIFE_MS

  const hits = await store.searchChannels({
    bankId: opts.bankId,
    query: opts.query,
    limit,
    channels,
    factTypes: opts.factTypes,
    timeRange: opts.timeRange,
  })

  // 候选 id：按通道顺序去重
  const candidateIds: string[] = []
  const seen = new Set<string>()
  for (const ch of hits) {
    for (const id of ch.ids) {
      if (!seen.has(id)) {
        seen.add(id)
        candidateIds.push(id)
      }
    }
  }

  const units = await store.getByIds(candidateIds)
  const unitById = new Map(units.map((u) => [u.id, u]))

  const useRrf = opts.fusion === 'rrf' || (opts.fusion !== 'native' && hits.length > 1)
  const base = useRrf ? rrfFuse(hits) : nativeScores(hits)

  // 最终得分 = 基础分 × 时间衰减
  const scores = new Map<string, number>()
  for (const id of candidateIds) {
    const unit = unitById.get(id)
    if (!unit) continue
    const b = base.get(id) ?? 0
    scores.set(id, b * decayFactor(recencyAnchor(unit), now, halfLifeMs))
  }

  // 按得分降序
  const candidates = candidateIds
    .filter((id) => unitById.has(id))
    .map((id) => unitById.get(id) as MemoryUnit)
    .sort((a, b) => (scores.get(b.id) ?? 0) - (scores.get(a.id) ?? 0))

  // 预算装箱
  const packed = packByBudget(candidates, (u) => u.text, {
    maxTokens: opts.maxTokens ?? MEMORY_INJECT_TOKENS,
  })

  return {
    units: packed.items,
    candidates,
    hits,
    scores,
    totalTokens: packed.totalTokens,
    dropped: packed.dropped,
    truncated: packed.truncated,
  }
}
