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
  /** 产物所属消息 id —— 动作产出的新产物要挂回同一条消息 */
  messageId?: Ref<string | undefined>,
) {
  const chatStore = useChatStore()

  /** 把产物挂到触发本动作的消息上（拿不到消息 id 就落到当前叶子） */
  function attachToMessage(art: ArtifactEnvelope) {
    const id = messageId?.value ?? chatStore.currentLeafId ?? undefined
    if (!id) {
      console.warn('[ArtifactAction] 没有可挂载的消息，产物被丢弃:', art.title)
      return
    }
    chatStore.attachArtifact(id, art)
  }

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
      emitArtifact: attachToMessage,
    }

    const result = await runAction(action, ctx)

    // 动作可以产出新产物（「形成产物链」）—— 之前这里把它丢掉了
    if (result.artifact) attachToMessage(result.artifact)

    actionState[action.id] = result.ok ? 'done' : 'idle'
    resultMessage.value = result.message ? `${result.ok ? '✓' : '✕'} ${result.message}` : ''

    if (result.ok) onSuccess?.(action)
  }

  return { actionState, resultMessage, actions, execute }
}
