/**
 * Agent 会话执行
 *
 * 负责「一次对话回合」的完整流程：
 *   1. 从消息树取当前路径，构造注入 LLM 的历史
 *   2. 调用 LangGraph Supervisor（线程按 session + branch 隔离）
 *   3. 把事件推给 UI，产物挂到消息上
 *
 * 关键点：
 * - 历史 = 当前分支路径（不是全部消息），所以切换分支上下文自动跟着变
 * - 不传 checkpointer：历史由消息树手动注入，是唯一事实来源，
 *   避免 LangGraph 把已存 checkpoint 与注入历史 concat 造成重复
 */

import { computed, ref } from 'vue'
import { AIMessage, HumanMessage } from '@langchain/core/messages'
import { useChatStore, type ChatMessage } from '@/stores/chatStore'
import { runSupervisorGraph } from '@/lib/agent'
import type { AgentEvent, AgentType } from '@/lib/agent/types'

export interface RunTurnOptions {
  /** 本轮对应的 user 消息 id（用于切片历史，避免 userInput 与历史重复） */
  anchorUserMessageId: string
  /** assistant 占位消息 id */
  assistantMessageId: string
}

export function useAgentSession() {
  const chatStore = useChatStore()

  const events = ref<AgentEvent[]>([])
  const loading = ref(false)
  const currentAgent = ref<AgentType>('supervisor')
  const lastError = ref<string | null>(null)
  const tokenUsage = ref<ChatMessage['tokenUsage']>(undefined)

  const artifactEvents = computed(() =>
    events.value.filter((e) => e.eventType === 'artifact' && !!e.payload),
  )
  const thoughtEvents = computed(() =>
    events.value.filter((e) => e.eventType === 'thought'),
  )

  /**
   * 构造注入 LLM 的历史：当前路径中，anchor 之前的消息。
   * 排除空 assistant 占位，且不包含 anchor 本身（它作为 userInput 单独传入）。
   */
  function buildHistory(anchorUserMessageId: string): Array<HumanMessage | AIMessage> {
    const path = chatStore.messages
    const idx = path.findIndex((m) => m.id === anchorUserMessageId)
    const before = idx >= 0 ? path.slice(0, idx) : path

    return before
      .filter((m) => m.role === 'user' || (m.role === 'assistant' && m.content))
      .map((m) =>
        m.role === 'user' ? new HumanMessage(m.content) : new AIMessage(m.content),
      )
  }

  /** 线程 id：会话 + 分支，保证分支之间 checkpoint 不串 */
  function threadIdFor(): string {
    const sessionId = chatStore.currentSession?.id ?? 'default'
    const leaf = chatStore.allMessages.find((m) => m.id === chatStore.currentLeafId)
    return `${sessionId}::${leaf?.branchId ?? 'main'}`
  }

  /** 从事件流与结果中提取最终回复 */
  function extractFinalAnswer(result: any): string {
    let finalAnswer: string = result?.finalResponse || ''

    if (!finalAnswer && result?.messages?.length) {
      const lastAI = [...result.messages].reverse().find((m: any) => m instanceof AIMessage)
      if (lastAI) {
        if (typeof lastAI.content === 'string') {
          finalAnswer = lastAI.content
        } else if (Array.isArray(lastAI.content)) {
          finalAnswer = lastAI.content
            .map((block: any) => (block.type === 'text' ? block.text : ''))
            .join('')
        }
      }
    }

    if (!finalAnswer) {
      const lastResponse = [...events.value].reverse().find((e) => e.eventType === 'response')
      if (lastResponse) finalAnswer = lastResponse.content
    }

    return finalAnswer
  }

  /** 执行一个对话回合 */
  async function runTurn(userText: string, opts: RunTurnOptions): Promise<void> {
    const { anchorUserMessageId, assistantMessageId } = opts

    loading.value = true
    chatStore.isLoading = true
    events.value = []
    lastError.value = null
    tokenUsage.value = undefined
    currentAgent.value = 'supervisor'

    const onEvent = (evt: AgentEvent) => {
      events.value.push(evt)

      if (evt.agent && evt.agent !== 'supervisor') {
        currentAgent.value = evt.agent
      }

      // 产物：直接挂到当前 assistant 消息上
      if (evt.eventType === 'artifact' && evt.payload) {
        chatStore.attachArtifact(assistantMessageId, evt.payload)
      }

      // 回复：实时更新气泡内容
      if (evt.eventType === 'response') {
        chatStore.updateMessage(assistantMessageId, { content: evt.content })
      }

      if (evt.eventType === 'error') {
        lastError.value = evt.content
      }
    }

    try {
      const history = buildHistory(anchorUserMessageId)
      const threadId = threadIdFor()

      const result = await runSupervisorGraph(
        userText,
        threadId,
        undefined, // 历史走手动注入，不用 checkpoint 恢复
        onEvent,
        history,
      )

      const finalAnswer = extractFinalAnswer(result) || 'Agent 未返回回复'

      const thoughts = thoughtEvents.value.map((e, i) => ({
        id: `lg-${i}`,
        content: e.content,
        timestamp: e.timestamp,
      }))

      chatStore.updateMessage(assistantMessageId, {
        content: finalAnswer,
        thoughts,
        tokenUsage: tokenUsage.value,
        error: undefined,
      })
    } catch (error) {
      const message = error instanceof Error ? error.message : String(error)
      console.error('[AgentSession] 回合执行失败:', error)
      lastError.value = message
      chatStore.updateMessage(assistantMessageId, {
        content: `抱歉，AI 服务暂时不可用：${message}`,
        error: message,
      })
    } finally {
      loading.value = false
      chatStore.isLoading = false
    }
  }

  /** 发送一条新消息 */
  async function send(text: string): Promise<void> {
    const t = text.trim()
    if (!t || loading.value) return

    const userMsg = chatStore.addMessage({ role: 'user', content: t })
    const assistantMsg = chatStore.addMessage({ role: 'assistant', content: '' })

    await runTurn(t, {
      anchorUserMessageId: userMsg.id,
      assistantMessageId: assistantMsg.id,
    })
  }

  /**
   * 重新生成某条 assistant 回复。
   * 会在同一父节点下产生一个兄弟分支。
   */
  async function regenerate(assistantMessageId: string): Promise<void> {
    if (loading.value) return
    const target = chatStore.allMessages.find((m) => m.id === assistantMessageId)
    if (!target) return

    const parent = target.parentId
      ? chatStore.allMessages.find((m) => m.id === target.parentId)
      : null
    const input = parent?.content || target.content
    if (!input) return

    const sibling = chatStore.addAssistantSibling(assistantMessageId)

    await runTurn(input, {
      anchorUserMessageId: parent?.id ?? assistantMessageId,
      assistantMessageId: sibling.id,
    })
  }

  /**
   * 编辑某条 user 消息并重发。
   * 新消息与原消息同父 → 形成新分支。
   */
  async function editAndSend(messageId: string, newContent: string): Promise<void> {
    const text = newContent.trim()
    if (!text || loading.value) return

    const userMsg = chatStore.editMessage(messageId, text)
    const assistantMsg = chatStore.addMessage({ role: 'assistant', content: '' })

    await runTurn(text, {
      anchorUserMessageId: userMsg.id,
      assistantMessageId: assistantMsg.id,
    })
  }

  /**
   * 从某条消息处分叉，继续对话。
   */
  async function forkAndSend(messageId: string, content: string): Promise<void> {
    const text = content.trim()
    if (!text || loading.value) return

    const userMsg = chatStore.forkFrom(messageId, text)
    const assistantMsg = chatStore.addMessage({ role: 'assistant', content: '' })

    await runTurn(text, {
      anchorUserMessageId: userMsg.id,
      assistantMessageId: assistantMsg.id,
    })
  }

  return {
    // 状态
    events,
    loading,
    currentAgent,
    lastError,
    tokenUsage,
    artifactEvents,
    thoughtEvents,

    // 操作
    send,
    regenerate,
    editAndSend,
    forkAndSend,
  }
}
