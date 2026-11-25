/**
 * Token 估算（跨模块共用）
 *
 * 记忆模块与 Agent 的工具结果预算共用同一套估算口径。
 */

/** 判断单个字符是否属于 CJK / 全角区间 */
function isWideChar(code: number): boolean {
  return (
    (code >= 0x3400 && code <= 0x9fff) ||
    (code >= 0xf900 && code <= 0xfaff) ||
    (code >= 0x3000 && code <= 0x303f) ||
    (code >= 0xff00 && code <= 0xffef)
  )
}

/** 估算 token 数：全角字符按 1，其余按 4 字符 1 token */
export function estimateTokens(text: string): number {
  if (!text) return 0
  let wide = 0
  for (let i = 0; i < text.length; i++) {
    if (isWideChar(text.charCodeAt(i))) wide++
  }
  const narrow = text.length - wide
  return Math.ceil(wide + narrow / 4)
}
