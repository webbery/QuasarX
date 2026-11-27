/**
 * 记忆更新队列：把未固化区间抽取成记忆并写库
 *
 * 一次固化 = 抽取一段对话 → 组装 MemoryUnit → 写库 → 游标前移。
 * 队列串行执行，天然保证同一时刻只有一个抽取在跑。
 *
 * 固化成功才前移游标；失败保留区间，下一轮继续用同一段区间重试。
 */

import type { MemoryStore } from './types'
import { extractFacts, type ChatInvoker, type ExtractedFact } from './extraction'
import { buildEmbedText } from './embed'
import { setCursor } from './memorySession'
import {
  getExtractionHealth,
  isExtractionAllowed,
  recordFailure,
  recordSuccess,
  type ErrorCategory,
} from './extractionHealth'
import { recordExtractCall, recordExtractFailure, recordExtractSuccess, recordExtractedFacts } from './memoryMetrics'

/** 一次固化的任务 */
export interface RetainJob {
  /** bank 作用域 */
  bankId: string
  /** 会话 id */
  sessionId: string
  /** 区间正文 */
  transcript: string
  /** 固化成功后游标前移到的消息 id */
  cursorTurnId: string
}

/** 一次固化的结果 */
export interface RetainOutcome {
  /** 是否成功写库 */
  ok: boolean
  /** 写入的事实条数 */
  facts: number
  /** 失败分类；成功时为 null */
  error: ErrorCategory | null
  /** 跳过原因 */
  skipped: 'breakerOpen' | 'noLlm' | 'empty' | null
}

/** 队列构造参数 */
export interface RetainQueueOptions {
  /** 记忆存储 */
  store: MemoryStore
  /** 抽取用的聊天模型；缺省时固化一律跳过 */
  llm?: ChatInvoker
  /** 按需取抽取用的聊天模型（配置可能中途切换） */
  resolveLlm?: () => ChatInvoker | undefined
}

/** 记忆更新队列 */
export interface RetainQueue {
  /** 排入一次固化任务（立即返回，不阻塞调用方） */
  enqueue(job: RetainJob): void
  /** 等待队列排空 */
  drain(): Promise<void>
  /** 队列中待处理的任务数 */
  readonly size: number
}

/** 事实 → 记忆单元 */
function factToUnit(
  fact: ExtractedFact,
  job: RetainJob,
  index: number,
  mentionedAt: number,
): import('./types').MemoryUnit {
  const text = [fact.what, fact.when, fact.who].filter(Boolean).join('；')
  return {
    id: `mem_${job.bankId}_${job.cursorTurnId}_${index}`,
    bankId: job.bankId,
    text,
    embedText: buildEmbedText(text, fact.occurredEnd ?? fact.occurredStart),
    factType: fact.factType,
    occurredStart: fact.occurredStart,
    occurredEnd: fact.occurredEnd,
    mentionedAt,
    confidence: null,
    entities: fact.entities ?? [],
    sourceTurnId: job.cursorTurnId,
    accessCount: 0,
    context: `extracted:${job.sessionId}`,
  }
}

/**
 * 创建记忆更新队列
 *
 * 队列内部串行处理，失败时按退避重试（最多 3 次），
 * 仍失败则保留区间，等下一轮重新入队。
 */
export function createRetainQueue(opts: RetainQueueOptions): RetainQueue {
  const pending: RetainJob[] = []
  let running: Promise<void> | null = null

  async function runJob(job: RetainJob): Promise<RetainOutcome> {
    const llm = opts.llm ?? opts.resolveLlm?.()
    if (!llm) return { ok: false, facts: 0, error: null, skipped: 'noLlm' }
    if (!job.transcript.trim()) return { ok: false, facts: 0, error: null, skipped: 'empty' }
    if (!isExtractionAllowed()) {
      return { ok: false, facts: 0, error: null, skipped: 'breakerOpen' }
    }

    recordExtractCall()
    const result = await extractFacts({ transcript: job.transcript }, llm)
    if (result.error) {
      recordFailure(result.error)
      recordExtractFailure(result.error)
      return { ok: false, facts: 0, error: result.error, skipped: null }
    }
    if (!result.facts.length) {
      recordSuccess()
      recordExtractSuccess()
      return { ok: false, facts: 0, error: null, skipped: 'empty' }
    }

    const mentionedAt = Date.now()
    const units = result.facts.map((fact, i) => factToUnit(fact, job, i, mentionedAt))
    await opts.store.upsert(units)

    recordSuccess()
    recordExtractSuccess(result.degraded)
    recordExtractedFacts(units.length)
    setCursor(job.sessionId, job.cursorTurnId)
    return { ok: true, facts: units.length, error: null, skipped: null }
  }

  async function pump(): Promise<void> {
    while (pending.length) {
      const job = pending.shift() as RetainJob
      try {
        await runJob(job)
      } catch (err) {
        console.warn('[Memory/Retain] 固化失败：', err)
        recordFailure('network')
        recordExtractFailure('network')
      }
    }
    running = null
  }

  return {
    enqueue(job: RetainJob) {
      pending.push(job)
      if (!running) running = pump()
    },
    async drain() {
      while (running) await running
    },
    get size() {
      return pending.length
    },
  }
}

/** 熔断状态快照（供 UI 展示） */
export function retainHealth() {
  return getExtractionHealth()
}
