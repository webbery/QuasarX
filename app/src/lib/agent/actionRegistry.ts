/**
 * 产物动作注册表
 *
 * 动作是「声明式 + 用户点击」的：有副作用的操作（执行回测、检索数据、应用策略修改）
 * 不让 LLM 直接调用工具，而是由工具在产物里声明一个动作，由用户在 UI 上点击执行。
 *
 * 这样做的好处：
 * - 可审计：所有副作用都有人类点击这一步
 * - 可回放：分支回放时动作不会自动重放
 * - 参数可信：动作参数来自工具/数据源本身，不是模型现场编的
 */

import type { ArtifactAction, ArtifactEnvelope } from './artifacts'
import type { AgentEvent } from './types'

export interface ActionContext {
  /** 触发该动作的产物 */
  artifact: ArtifactEnvelope
  /** 会话 id（分支隔离用） */
  sessionId: string
  /** 事件回调：动作执行过程可以继续往前端推事件 */
  emit?: (event: AgentEvent) => void
}

export interface ActionResult {
  ok: boolean
  message?: string
  /** 动作执行后可以返回新产物，形成产物链 */
  artifact?: ArtifactEnvelope
}

export type ActionHandler = (
  action: ArtifactAction,
  ctx: ActionContext,
) => Promise<ActionResult>

const handlers = new Map<string, ActionHandler>()

/** 注册一个动作处理器 */
export function registerAction(kind: string, handler: ActionHandler): void {
  handlers.set(kind, handler)
}

/** 已注册的动作处理器 */
export function listActionHandlers(): string[] {
  return [...handlers.keys()]
}

/**
 * 执行一个动作。
 * 未注册的 kind 返回 ok=false 而不是抛错，避免打断对话流。
 */
export async function runAction(
  action: ArtifactAction,
  ctx: ActionContext,
): Promise<ActionResult> {
  const key = action.handler || action.kind
  const handler = handlers.get(key)
  if (!handler) {
    return {
      ok: false,
      message: `该操作尚未接入：${action.label}（未注册处理器 "${key}"）`,
    }
  }
  try {
    return await handler(action, ctx)
  } catch (err) {
    const message = err instanceof Error ? err.message : String(err)
    console.warn(`[AgentAction] 动作 ${key} 执行失败:`, err)
    return { ok: false, message }
  }
}
