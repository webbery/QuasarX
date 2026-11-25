/**
 * bank 作用域
 *
 * bank 是记忆的硬边界：不同 bank 的记忆互不可见。
 * 当前取值为 session id —— 同一 session 内的所有 agent（supervisor / chat / strategy …）
 * 共用一个 bank。
 */

/** bank id 前缀 */
export const BANK_PREFIX = 'quasarx::'

/** 无法解析出作用域时的兜底 bank */
export const DEFAULT_BANK_ID = `${BANK_PREFIX}default`

/** 由 session id 推出 bank id */
export function bankIdFromSession(sessionId: string): string {
  const id = (sessionId ?? '').trim()
  return id ? `${BANK_PREFIX}${id}` : DEFAULT_BANK_ID
}

/** 由项目 id 推出 bank id */
export function bankIdFromProject(projectId: string): string {
  const id = (projectId ?? '').trim()
  return id ? `${BANK_PREFIX}${id}` : DEFAULT_BANK_ID
}

/** 从 bank id 反解 session id；不是 session 型 bank 时返回 null */
export function sessionIdFromBank(bankId: string): string | null {
  if (!bankId || !bankId.startsWith(BANK_PREFIX)) return null
  const rest = bankId.slice(BANK_PREFIX.length)
  return rest && rest !== 'default' ? rest : null
}

/** 判断两个 bank id 是否指向同一个作用域 */
export function isSameBank(a: string, b: string): boolean {
  return a === b
}
