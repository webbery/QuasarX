/**
 * 会话级记忆状态：固化游标 + 未固化区间 + token 计数
 *
 * 「固化」= 把一段对话抽取成记忆并写库。固化成功后游标前移，
 * 该区间不再重复发送、也不再重复抽取。
 *
 * 本模块只管状态与纯计算，不碰存储、不碰 LLM。
 * 游标存 localStorage，键为 `quasarx::memory-cursor::<sessionId>`。
 */

import { estimateTokens } from '../tokens'

/** 记忆更新阈值：未固化区间的正文 token 超过它就触发一次记忆更新 */
export const MEMORY_UPDATE_TOKEN_THRESHOLD = 1_000_000

/** 游标 key 前缀 */
export const MEMORY_CURSOR_KEY_PREFIX = 'quasarx::memory-cursor::'

/** 参与固化判断的一条对话消息（与 chatStore 的 ChatMessage 解耦） */
export interface ConversationTurn {
  /** 消息 id */
  id: string
  /** 说话方 */
  role: 'user' | 'assistant'
  /** 正文 */
  content: string
}

/** 未固化区间：自上次固化以来、尚未写进记忆的那段对话 */
export interface PendingInterval {
  /** 区间内的消息（按对话顺序） */
  turns: ConversationTurn[]
  /** 区间正文的 token 数（estimateTokens） */
  tokens: number
  /** 区间最后一条消息 id；固化成功后作为新游标 */
  lastTurnId: string | null
  /** 游标是否命中了当前路径（false = 路径被分叉打断，整条路径都算未固化） */
  cursorFound: boolean
}

/** localStorage 不可用时（Node / SSR / 隐私模式）返回 null */
function safeStorage(): Storage | null {
  try {
    if (typeof localStorage === 'undefined') return null
    return localStorage
  } catch {
    return null
  }
}

/** 游标 key */
export function cursorKey(sessionId: string): string {
  return `${MEMORY_CURSOR_KEY_PREFIX}${sessionId}`
}

/** 读取固化游标（上次固化覆盖到的最后一条消息 id）；没有则 null */
export function getCursor(sessionId: string): string | null {
  const store = safeStorage()
  if (!store || !sessionId) return null
  try {
    return store.getItem(cursorKey(sessionId))
  } catch {
    return null
  }
}

/** 写固化游标 */
export function setCursor(sessionId: string, turnId: string): void {
  const store = safeStorage()
  if (!store || !sessionId || !turnId) return
  try {
    store.setItem(cursorKey(sessionId), turnId)
  } catch {
    /* 配额满 / 隐私模式：忽略。游标丢失只会导致重复固化，不会丢数据 */
  }
}

/** 清游标（会话删除时） */
export function clearCursor(sessionId: string): void {
  const store = safeStorage()
  if (!store || !sessionId) return
  try {
    store.removeItem(cursorKey(sessionId))
  } catch {
    /* 忽略 */
  }
}

/** 区间正文文本（既是注入内容，也是抽取输入） */
export function renderIntervalText(turns: ConversationTurn[]): string {
  return turns
    .filter((t) => t.content && t.content.trim())
    .map((t) => `${t.role === 'user' ? '用户' : '助手'}：${t.content.trim()}`)
    .join('\n')
}

/** 区间正文的 token 数 */
export function intervalTokens(turns: ConversationTurn[]): number {
  return estimateTokens(renderIntervalText(turns))
}

/**
 * 把当前会话的消息路径切成「未固化区间」
 *
 * 游标不在路径上（分叉 / 消息被裁剪）时保守处理：整条路径都算未固化。
 * 宁可多固化一次，也不要漏掉一段对话。
 */
export function pendingInterval(sessionId: string, path: ConversationTurn[]): PendingInterval {
  const turns = path.filter(
    (t) => (t.role === 'user' || t.role === 'assistant') && !!t.content,
  )
  const cursor = getCursor(sessionId)
  if (!cursor) {
    return {
      turns,
      tokens: intervalTokens(turns),
      lastTurnId: turns.length ? turns[turns.length - 1].id : null,
      cursorFound: false,
    }
  }
  const idx = turns.findIndex((t) => t.id === cursor)
  if (idx < 0) {
    return {
      turns,
      tokens: intervalTokens(turns),
      lastTurnId: turns.length ? turns[turns.length - 1].id : null,
      cursorFound: false,
    }
  }
  const slice = turns.slice(idx + 1)
  return {
    turns: slice,
    tokens: intervalTokens(slice),
    lastTurnId: slice.length ? slice[slice.length - 1].id : null,
    cursorFound: true,
  }
}

/** 是否达到记忆更新阈值（严格大于） */
export function shouldUpdateMemory(
  tokens: number,
  threshold: number = MEMORY_UPDATE_TOKEN_THRESHOLD,
): boolean {
  return Number.isFinite(tokens) && tokens > threshold
}
