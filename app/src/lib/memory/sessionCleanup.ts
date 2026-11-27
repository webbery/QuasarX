/**
 * 会话删除时的记忆清理
 *
 * 会话被删除 → 该会话 bank 下的记忆一并清掉，固化游标也一并清掉，
 * 避免会话复活后接着上一次的固化区间继续算。
 */

import { bankIdFromSession } from './bank'
import { clearCursor } from './memorySession'
import { getMemoryStore } from './storeProvider'

/**
 * 清空某个会话的记忆
 *
 * 失败只告警不抛：记忆清理不该阻塞会话删除。
 */
export async function purgeSessionMemories(sessionId: string): Promise<void> {
  if (!sessionId) return
  try {
    const store = getMemoryStore()
    await store.deleteByBank(bankIdFromSession(sessionId))
    clearCursor(sessionId)
  } catch (e) {
    console.warn('[Memory] 清理会话记忆失败：', sessionId, e)
  }
}
