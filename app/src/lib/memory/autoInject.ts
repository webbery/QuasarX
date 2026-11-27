/**
 * 记忆上下文的自动注入（每轮开始时调用）
 *
 * 注入块 = 召回的记忆 + 未固化区间的原文。
 *
 * 未固化区间的正文 token 超过阈值时，本轮返回 shouldUpdate = true，
 * 由 retainQueue 安排一次记忆更新；未超过时区间原文直接随上下文发送。
 *
 * 两段都没有内容时返回 context = null，调用方据此一条消息都不插入。
 *
 * store 由调用方注入，宿主差异不落到本模块。
 */

import type { MemoryStore, MemoryUnit } from './types'
import { recall } from './recall'
import { renderMemoryBlock, MEMORY_INJECT_TOKENS } from './budget'
import { recordRecall } from './memoryMetrics'
import {
  MEMORY_UPDATE_TOKEN_THRESHOLD,
  pendingInterval,
  renderIntervalText,
  shouldUpdateMemory,
  type ConversationTurn,
  type PendingInterval,
} from './memorySession'

/** 注入块里「记忆」那一段的标题 */
export const MEMORY_SECTION_TITLE = '【记忆】'

/** 注入块里「未固化对话」那一段的标题 */
export const PENDING_SECTION_TITLE = '【尚未写入记忆的对话】'

/** 构造注入块的输入 */
export interface AutoInjectInput {
  /** 记忆存储 */
  store: MemoryStore
  /** bank 作用域 */
  bankId: string
  /** 本轮用户输入（召回的查询串） */
  userInput: string
  /** 会话 id（定位固化游标） */
  sessionId: string
  /** 当前会话的消息路径（当前分支） */
  path: ConversationTurn[]
  /** 记忆部分的 token 上限 */
  maxTokens?: number
  /** 记忆更新阈值 */
  threshold?: number
}

/** 构造结果 */
export interface AutoInjectResult {
  /** 注入上下文；null 表示本轮不注入 */
  context: string | null
  /** 召回的记忆（按相关性降序） */
  recalled: MemoryUnit[]
  /** 召回部分的 token 数 */
  recallTokens: number
  /** 未固化区间 */
  pending: PendingInterval
  /** 未固化区间的 token 数 */
  pendingTokens: number
  /** 是否达到记忆更新阈值 */
  shouldUpdate: boolean
}

/**
 * 构造本轮要注入的记忆上下文
 *
 * 召回失败时降级成只带未固化区间，不影响对话继续。
 */
export async function buildMemoryContext(input: AutoInjectInput): Promise<AutoInjectResult> {
  const threshold = input.threshold ?? MEMORY_UPDATE_TOKEN_THRESHOLD
  const pending = pendingInterval(input.sessionId, input.path ?? [])

  let recalled: MemoryUnit[] = []
  let recallTokens = 0
  try {
    const started = Date.now()
    const result = await recall(input.store, {
      bankId: input.bankId,
      query: input.userInput,
      maxTokens: input.maxTokens ?? MEMORY_INJECT_TOKENS,
    })
    recalled = result.units
    recallTokens = result.totalTokens
    recordRecall(recallTokens, Date.now() - started)
  } catch (err) {
    console.warn('[Memory/Inject] 召回失败，本轮只带未固化区间：', err)
  }

  const sections: string[] = []
  if (recalled.length) {
    const rendered = renderMemoryBlock(recalled, input.maxTokens ?? MEMORY_INJECT_TOKENS)
    recallTokens = rendered.packed.totalTokens
    sections.push(`${MEMORY_SECTION_TITLE}\n${rendered.text}`)
  }

  const pendingText = renderIntervalText(pending.turns)
  if (pendingText.trim()) {
    sections.push(`${PENDING_SECTION_TITLE}\n${pendingText}`)
  }

  return {
    context: sections.length ? sections.join('\n\n') : null,
    recalled,
    recallTokens,
    pending,
    pendingTokens: pending.tokens,
    shouldUpdate: shouldUpdateMemory(pending.tokens, threshold),
  }
}
