/**
 * 产物动作的执行逻辑（AgentArtifactCard 与 UiFrame 共用）
 *
 * 动作是「声明式 + 用户点击」的：有副作用的操作由用户在 UI 上点击才执行。
 */

import { computed, reactive, ref, type Ref } from 'vue'
import { runAction, type ActionContext } from '@/lib/agent/actionRegistry'
import type { ArtifactAction, ArtifactEnvelope } from '@/lib/agent/artifacts'
import { useChatStore } from '@/stores/chatStore'

export function useArtifactActions(
  artifact: Ref<ArtifactEnvelope | undefined>,
  onSuccess?: (action: ArtifactAction) => void,
) {
  const chatStore = useChatStore()

  const actionState = reactive<Record<string, 'idle' | 'pending' | 'done'>>({})
  const resultMessage = ref('')

  /** 动作来源：顶层 actions 优先，其次 action 类型 payload.actions */
  const actions = computed<ArtifactAction[]>(() => {
    const a = artifact.value
    if (!a) return []
    if (a.actions?.length) return a.actions
    const payload = a.payload as { actions?: ArtifactAction[] } | undefined
    if (a.kind === 'action' && payload?.actions?.length) return payload.actions
    return []
  })

  async function execute(action: ArtifactAction) {
    const a = artifact.value
    if (!a) return

    if (action.destructive && action.confirm) {
      const ok = window.confirm(
        `${action.confirm.title}${action.confirm.body ? `\n\n${action.confirm.body}` : ''}`,
      )
      if (!ok) return
    }

    actionState[action.id] = 'pending'
    resultMessage.value = ''

    const ctx: ActionContext = {
      artifact: a,
      sessionId: chatStore.currentSession?.id ?? 'default',
    }

    const result = await runAction(action, ctx)

    actionState[action.id] = result.ok ? 'done' : 'idle'
    resultMessage.value = result.message ? `${result.ok ? '✓' : '✕'} ${result.message}` : ''

    if (result.ok) onSuccess?.(action)
  }

  return { actionState, resultMessage, actions, execute }
}
