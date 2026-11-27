/**
 * Agent 会话执行
 *
 * 负责「一次对话回合」的完整流程：
 *   1. 从消息树取当前路径，构造注入 LLM 的历史
 *   2. 调用 LangGraph Supervisor（线程按 session + branch 隔离）
 *   3. 把事件推给 UI，产物挂到消息上
 *   4. 记忆：回合开始注入记忆上下文，回合结束按阈值排队一次记忆更新
 *
 * 关键点：
 * - 历史 = 当前分支路径（不是全部消息），所以切换分支上下文自动跟着变
 * - 不传 checkpointer：历史由消息树手动注入，是唯一事实来源，
 *   避免 LangGraph 把已存 checkpoint 与注入历史 concat 造成重复
 * - 记忆开关关闭时，本回合完全不接触记忆存储
 */

import { computed, ref } from 'vue'
import { AIMessage, HumanMessage } from '@langchain/core/messages'
import { useChatStore, type ChatMessage } from '@/stores/chatStore'
import { useMemoryStatsStore } from '@/stores/memoryStats'
import { createLLM, getAgentConfig, runSupervisorGraph, type AgentConfig } from '@/lib/agent'
import type { AgentEvent, AgentType, MemoryRuntime, UsageReporter } from '@/lib/agent/types'
import { bankIdFromSession } from '@/lib/memory/bank'
import { buildMemoryContext } from '@/lib/memory/autoInject'
import { recordInjection } from '@/lib/memory/memoryMetrics'
import {
  pendingInterval,
  renderIntervalText,
  shouldUpdateMemory,
  type ConversationTurn,
} from '@/lib/memory/memorySession'
import { createRetainQueue, type RetainQueue } from '@/lib/memory/retainQueue'
import { getMemoryStore } from '@/lib/memory/storeProvider'
import type { MemoryStore } from '@/lib/memory/types'
import { estimateTokens } from '@/lib/tokens'

export interface RunTurnOptions {
  /** 本轮对应的 user 消息 id（用于切片历史，避免 userInput 与历史重复） */
  anchorUserMessageId: string
  /** assistant 占位消息 id */
  assistantMessageId: string
}

/** 错误类型分类 */
export type ErrorType = 'rate_limit' | 'aborted' | 'network' | 'unknown'

/** 解析错误信息，返回错误类型 */
function classifyError(message: string): ErrorType {
  const lower = message.toLowerCase()
  if (lower.includes('429') || lower.includes('too many requests') || lower.includes('quota') || lower.includes('rate limit')) {
    return 'rate_limit'
  }
  if (lower.includes('abort') || lower.includes('cancel')) {
    return 'aborted'
  }
  if (lower.includes('network') || lower.includes('fetch') || lower.includes('timeout')) {
    return 'network'
  }
  return 'unknown'
}

/** 记忆更新队列：模块级单例，整个应用共用一条 */
let retainQueue: RetainQueue | null = null

/** 抽取用的模型配置快照；每轮开始时更新 */
let retainConfig: AgentConfig | null = null

/**
 * 取（或懒创建）记忆更新队列
 *
 * 队列串行执行，同一时刻只有一个抽取在跑。
 */
function getRetainQueue(store: MemoryStore): RetainQueue {
  if (!retainQueue) {
    retainQueue = createRetainQueue({
      store,
      resolveLlm: () => {
        const cfg = retainConfig
        if (!cfg) return undefined
        // 抽取在后台跑，重试 0 次：默认重试在 429 时会退避十几分钟
        return createLLM(cfg, 0)
      },
    })
  }
  return retainQueue
}

/** 当前分支的消息 → 记忆模块的对话轮次 */
function toConversationTurns(messages: ChatMessage[]): ConversationTurn[] {
  const turns: ConversationTurn[] = []
  for (const m of messages) {
    if (m.role !== 'user' && m.role !== 'assistant') continue
    if (!m.content || !m.content.trim()) continue
    turns.push({ id: m.id, role: m.role, content: m.content })
  }
  return turns
}

export function useAgentSession() {
  const chatStore = useChatStore()
  const memoryStore = useMemoryStatsStore()

  const events = ref<AgentEvent[]>([])
  const loading = ref(false)
  const currentAgent = ref<AgentType>('supervisor')
  const lastError = ref<string | null>(null)
  const lastErrorType = ref<ErrorType>('unknown')
  const tokenUsage = ref<ChatMessage['tokenUsage']>(undefined)
  const abortController = ref<AbortController | null>(null)

  const artifactEvents = computed(() =>
    events.value.filter((e) => e.eventType === 'artifact' && !!e.payload),
  )
  const thoughtEvents = computed(() =>
    events.value.filter((e) => e.eventType === 'thought'),
  )

  /**
   * 持久化到消息里的思考记录
   *
   * 能力缺口（gap）也一起持久化 —— 回合结束后工作流时间线会清空，
   * 但「这件事做不了」是用户需要带走的结论，不能只活在时间线里。
   */
  const persistedThoughts = computed(() =>
    events.value.filter((e) => e.eventType === 'thought' || e.eventType === 'gap'),
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

  /**
   * 回合开始：构造本轮的记忆运行时
   *
   * 记忆关闭时直接返回 —— 本轮完全不碰记忆存储。
   * 存储不可用或上下文构造失败时也退化成「本轮不注入记忆」，对话照常继续。
   */
  async function prepareMemory(
    userText: string,
  ): Promise<{ runtime?: MemoryRuntime; store?: MemoryStore }> {
    if (!memoryStore.enabled) return {}

    let store: MemoryStore
    try {
      store = getMemoryStore()
    } catch (e) {
      console.warn('[Memory] 记忆存储不可用，本轮不启用记忆：', e)
      return {}
    }

    const sessionId = chatStore.currentSession?.id ?? 'default'
    const bankId = bankIdFromSession(sessionId)

    try {
      const result = await buildMemoryContext({
        store,
        bankId,
        userInput: userText,
        sessionId,
        path: toConversationTurns(chatStore.messages),
        threshold: memoryStore.threshold,
      })
      if (result.context) recordInjection(estimateTokens(result.context))
      return { runtime: { context: result.context, store, bankId }, store }
    } catch (e) {
      console.warn('[Memory] 构造记忆上下文失败，本轮不注入记忆：', e)
      return { store }
    }
  }

  /**
   * 回合结束：未固化区间超过阈值时排队一次记忆更新
   *
   * 排队不 await：记忆写入在后台串行跑，不阻塞本轮回复。
   * 用户中断本轮对话不会影响已经排队的记忆写入。
   */
  function scheduleMemoryUpdate(store?: MemoryStore) {
    if (!memoryStore.enabled || !store) return

    const sessionId = chatStore.currentSession?.id ?? 'default'
    const bankId = bankIdFromSession(sessionId)
    const pending = pendingInterval(sessionId, toConversationTurns(chatStore.messages))
    if (!shouldUpdateMemory(pending.tokens, memoryStore.threshold)) return
    if (!pending.lastTurnId) return

    try {
      getRetainQueue(store).enqueue({
        bankId,
        sessionId,
        transcript: renderIntervalText(pending.turns),
        cursorTurnId: pending.lastTurnId,
      })
    } catch (e) {
      console.warn('[Memory] 排队记忆更新失败：', e)
    }
  }

  /** 执行一个对话回合 */
  async function runTurn(userText: string, opts: RunTurnOptions): Promise<void> {
    const { anchorUserMessageId, assistantMessageId } = opts

    loading.value = true
    chatStore.isLoading = true
    events.value = []
    lastError.value = null
    lastErrorType.value = 'unknown'
    tokenUsage.value = undefined
    currentAgent.value = 'supervisor'

    // 每轮开始取一次配置快照：模型基底在回合中途切换也不会让图节点混用模型
    const config = getAgentConfig()
    if (!config) {
      loading.value = false
      chatStore.isLoading = false
      lastError.value = '请先在设置中配置模型基底（URL / 模型 / API Key）'
      chatStore.updateMessage(assistantMessageId, { content: '', error: undefined })
      return
    }

    // 抽取用的模型配置快照：回合中途换模型不影响已入队的记忆更新
    retainConfig = config

    // 创建 AbortController 用于中止请求
    const controller = new AbortController()
    abortController.value = controller

    const onUsage: UsageReporter = (u) => {
      const cur = tokenUsage.value
      tokenUsage.value = {
        promptTokens: (cur?.promptTokens ?? 0) + u.promptTokens,
        completionTokens: (cur?.completionTokens ?? 0) + u.completionTokens,
        totalTokens: (cur?.totalTokens ?? 0) + u.totalTokens,
      }
    }

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

    // 回合开始：召回记忆 + 未固化对话，注入到本轮上下文
    const memory = await prepareMemory(userText)

    try {
      const history = buildHistory(anchorUserMessageId)
      const threadId = threadIdFor()

      const result = await runSupervisorGraph(
        userText,
        threadId,
        undefined, // 历史走手动注入，不用 checkpoint 恢复
        onEvent,
        history,
        onUsage,
        controller.signal,
        config,
        memory.runtime,
      )

      const finalAnswer = extractFinalAnswer(result) || 'Agent 未返回回复'

      const thoughts = persistedThoughts.value.map((e, i) => ({
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

      // 回合结束：未固化区间超过阈值时排队一次记忆更新
      scheduleMemoryUpdate(memory.store)
    } catch (error) {
      // 以「本回合的 controller 是否被 abort」为准，底层 SDK 抛的错名各不相同
      const aborted = controller.signal.aborted
      const rawMessage = aborted
        ? '请求已取消'
        : error instanceof Error
          ? error.message
          : String(error)
      const errorType: ErrorType = aborted ? 'aborted' : classifyError(rawMessage)
      const friendly = aborted
        ? '请求已取消'
        : errorType === 'rate_limit'
          ? 'AI 服务额度已用完，请稍后重试或充值 API 额度'
          : `AI 服务暂时不可用：${rawMessage}`

      if (!aborted) {
        console.error('[AgentSession] 回合执行失败:', error)
      }
      // 错误只在状态栏展示具体原因，不占用 AI 的回复栏
      lastError.value = friendly
      lastErrorType.value = errorType
      chatStore.updateMessage(assistantMessageId, {
        content: '',
        error: undefined,
      })
    } finally {
      abortController.value = null
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

  /**
   * 中止当前正在进行的请求
   */
  function abort() {
    if (abortController.value) {
      abortController.value.abort()
      abortController.value = null
    }
  }

  return {
    // 状态
    events,
    loading,
    currentAgent,
    lastError,
    lastErrorType,
    tokenUsage,
    artifactEvents,
    thoughtEvents,

    // 操作
    send,
    regenerate,
    editAndSend,
    forkAndSend,
    abort,
  }
}
