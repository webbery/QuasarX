/**
 * 上下文预算
 *
 * 在工具结果写入 ToolMessage 之前统一限流：
 * - 单条结果上限 TOOL_RESULT_MAX_TOKENS
 * - 单次 Agent run 累计上限 RUN_CONTEXT_BUDGET_TOKENS
 *
 * 超限时保留首尾并用标记替换中段；预算耗尽后只回一段提示。
 */

import { estimateTokens } from '../tokens'

/** 单条工具结果注入上限（估算 token） */
export const TOOL_RESULT_MAX_TOKENS = 4_000

/** 单次 Agent run 的工具结果累计上限（估算 token） */
export const RUN_CONTEXT_BUDGET_TOKENS = 40_000

/** 预算水位，按 run 维度累加 */
export interface BudgetState {
  usedTokens: number
}

/** 一次预算处理的结果 */
export interface BudgetedResult {
  text: string
  /** 是否发生了截断 */
  truncated: boolean
  /** 原始字符数 */
  originalChars: number
  /** 实际注入的估算 token */
  injectedTokens: number
  /** 预算是否已耗尽 */
  exhausted: boolean
}

export interface BudgetOptions {
  perResultMaxTokens?: number
  runMaxTokens?: number
}

/** 估算 token 数（实现见 src/lib/tokens.ts） */
export { estimateTokens } from '../tokens'

/** 保留首尾、替换中段 */
function truncateMiddle(text: string, maxChars: number, omittedTokens: number): string {
  if (text.length <= maxChars) return text
  const marker =
    `\n\n……（已按上下文预算截断：原 ${text.length} 字符，省略约 ${omittedTokens} tokens；` +
    `需要完整数据请缩小查询范围，或提示用户查看产物卡）……\n\n`
  const keep = maxChars - marker.length
  if (keep <= 0) return text.slice(0, Math.max(0, maxChars))
  const head = Math.floor(keep * 0.65)
  const tail = keep - head
  return `${text.slice(0, head)}${marker}${text.slice(-tail)}`
}

/**
 * 对一条工具结果应用上下文预算，并累加 run 水位。
 *
 * 结果不超过上限时原样返回；超限时按首尾保留截断；
 * run 预算耗尽后返回提示文本，不再注入原始内容。
 */
export function budgetToolResult(
  text: string,
  state: BudgetState,
  opts?: BudgetOptions,
): BudgetedResult {
  const perResultMax = opts?.perResultMaxTokens ?? TOOL_RESULT_MAX_TOKENS
  const runMax = opts?.runMaxTokens ?? RUN_CONTEXT_BUDGET_TOKENS

  const originalTokens = estimateTokens(text)
  const remaining = runMax - state.usedTokens

  if (remaining <= 0) {
    const notice =
      `（上下文预算已用尽：该工具返回的 ${text.length} 字符结果未注入。` +
      `请缩小查询范围或换更精确的参数重试；完整数据请提示用户在产物卡中查看。）`
    const injectedTokens = estimateTokens(notice)
    state.usedTokens += injectedTokens
    return {
      text: notice,
      truncated: true,
      originalChars: text.length,
      injectedTokens,
      exhausted: true,
    }
  }

  const allowedTokens = Math.min(perResultMax, remaining)
  if (originalTokens <= allowedTokens) {
    state.usedTokens += originalTokens
    return {
      text,
      truncated: false,
      originalChars: text.length,
      injectedTokens: originalTokens,
      exhausted: false,
    }
  }

  // 按本文本自身的字符/token 密度换算可保留的字符数
  const charsPerToken = text.length / Math.max(1, originalTokens)
  const allowedChars = Math.max(200, Math.floor(allowedTokens * charsPerToken))
  const clipped = truncateMiddle(text, allowedChars, originalTokens - allowedTokens)
  const injectedTokens = estimateTokens(clipped)
  state.usedTokens += injectedTokens

  return {
    text: clipped,
    truncated: true,
    originalChars: text.length,
    injectedTokens,
    exhausted: false,
  }
}
