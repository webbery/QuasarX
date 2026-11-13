/**
 * 对话 Store —— 消息树 + 会话 + 产物
 *
 * 与旧版的区别：
 * - 消息从「平铺数组」改成「带 parentId 指针的树」，支持分支
 * - 新增 ChatSession，多会话隔离
 * - 产物（Artifact）单独存放，消息只持有 artifactIds 引用
 *
 * 树的存储方式是「扁平数组 + parentId 指针」：
 * - 视图 = pathTo(currentLeafId)，即从根到当前叶子的路径
 * - 分支 = 同一个父节点下的多个子节点
 * - 切换分支 = 改变 currentLeafId
 */

import { defineStore } from 'pinia'
import { computed, ref } from 'vue'
import { useQuoteStore } from './quoteStore'
import { useAccountStore } from './account'
import { fetchSectorQuotes, calculateTotalAdvanceDecline } from '@/lib/sectorApi'
import type { ArtifactEnvelope } from '@/lib/agent/artifacts'

/* ============================================================================
 * 类型
 * ========================================================================== */

export interface ThoughtStep {
  id: string
  content: string        // 思考内容（灰体字）
  toolName?: string      // 调用的工具名称
  timestamp: number
}

export interface TokenUsage {
  promptTokens: number
  completionTokens: number
  totalTokens: number
}

export interface ChatMessage {
  id: string
  /** 父消息 id；null = 分支根 */
  parentId: string | null
  /** 所属分支 id（同一条对话路径共享一个 branchId，分叉时新建） */
  branchId: string
  role: 'user' | 'assistant' | 'system'
  content: string
  timestamp: number
  thoughts?: ThoughtStep[]
  tokenUsage?: TokenUsage
  /** 该消息产出的产物 id 列表 */
  artifactIds?: string[]
  /** 错误信息（assistant 消息失败时） */
  error?: string
}

export interface ChatSession {
  id: string
  title: string
  messages: ChatMessage[]
  /** 当前查看路径的末端消息 id */
  currentLeafId: string | null
  createdAt: number
  updatedAt: number
}

/* ============================================================================
 * 常量
 * ========================================================================== */

const SESSIONS_KEY = 'quasarx_chat_sessions_v2'
const ARTIFACTS_KEY = 'quasarx_chat_artifacts'
const LEGACY_KEY = 'quasarx_chat'

/** 最多保留的会话数 */
const MAX_SESSIONS = 20
/** 单个会话最多保留的消息数（超出时裁剪最老的根分支） */
const MAX_MESSAGES = 800
/** 最多保留的产物数 */
const MAX_ARTIFACTS = 40

let idSeq = 0
function uid(prefix: string): string {
  idSeq += 1
  return `${prefix}-${Date.now().toString(36)}-${idSeq.toString(36)}`
}

/* ============================================================================
 * 工具函数
 * ========================================================================== */

/** 从根到指定叶子的消息路径 */
export function pathTo(session: ChatSession, leafId: string | null): ChatMessage[] {
  if (!leafId) return []
  const byId = new Map(session.messages.map((m) => [m.id, m]))
  const chain: ChatMessage[] = []
  const guard = new Set<string>()
  let cur = byId.get(leafId)
  while (cur && !guard.has(cur.id)) {
    guard.add(cur.id)
    chain.push(cur)
    cur = cur.parentId ? byId.get(cur.parentId) : undefined
  }
  return chain.reverse()
}

/** 以某个节点为起点，向下找到最深的叶子（取最近创建的子节点） */
function deepestLeaf(session: ChatSession, nodeId: string): string {
  let cur = nodeId
  const guard = new Set<string>()
  for (;;) {
    if (guard.has(cur)) return cur
    guard.add(cur)
    const children = session.messages.filter((m) => m.parentId === cur)
    if (!children.length) return cur
    children.sort((a, b) => a.timestamp - b.timestamp)
    cur = children[children.length - 1].id
  }
}

/* ============================================================================
 * Store
 * ========================================================================== */

export const useChatStore = defineStore('chat', () => {
  /* ---------- 基础状态 ---------- */
  const visible = ref(false)
  const isLoading = ref(false)
  const marketContext = ref('')
  const useLangGraph = ref(true)

  /* ---------- 会话树 ---------- */
  const sessions = ref<ChatSession[]>([])
  const currentSessionId = ref('')
  const artifacts = ref<Record<string, ArtifactEnvelope>>({})

  const currentSession = computed<ChatSession | null>(
    () => sessions.value.find((s) => s.id === currentSessionId.value) ?? null,
  )

  /** 当前会话的全部消息（树的扁平存储，不含路径筛选） */
  const allMessages = computed<ChatMessage[]>(() => currentSession.value?.messages ?? [])

  /** 当前路径的消息（UI 展示用） */
  const messages = computed<ChatMessage[]>(() =>
    currentSession.value ? pathTo(currentSession.value, currentSession.value.currentLeafId) : [],
  )

  const currentLeafId = computed<string | null>(() => currentSession.value?.currentLeafId ?? null)

  /** 路径上的 user/assistant 消息，可直接转成 LLM 历史 */
  const historyMessages = computed<ChatMessage[]>(() =>
    messages.value.filter((m) => m.role === 'user' || (m.role === 'assistant' && m.content)),
  )

  /* ---------- 持久化 ---------- */

  function persistSessions() {
    try {
      const trimmed = sessions.value.slice(-MAX_SESSIONS).map((s) => ({
        ...s,
        messages: s.messages.slice(-MAX_MESSAGES),
      }))
      localStorage.setItem(SESSIONS_KEY, JSON.stringify(trimmed))
    } catch (e) {
      console.warn('[Chat] 保存会话失败', e)
    }
  }

  function persistArtifacts() {
    try {
      const entries = Object.values(artifacts.value)
      if (entries.length > MAX_ARTIFACTS) {
        entries.sort((a, b) => a.createdAt - b.createdAt)
        const keep = new Set(entries.slice(-MAX_ARTIFACTS).map((a) => a.id))
        const next: Record<string, ArtifactEnvelope> = {}
        for (const key of Object.keys(artifacts.value)) {
          if (keep.has(key)) next[key] = artifacts.value[key]
        }
        artifacts.value = next
      }
      localStorage.setItem(ARTIFACTS_KEY, JSON.stringify(artifacts.value))
    } catch (e) {
      // 产物可能很大（含时间序列），写不下时降级为不持久化
      console.warn('[Chat] 保存产物失败（可能超出存储配额），本次产物仅保留在内存', e)
    }
  }

  function loadFromStorage() {
    try {
      const rawArtifacts = localStorage.getItem(ARTIFACTS_KEY)
      if (rawArtifacts) artifacts.value = JSON.parse(rawArtifacts)

      const raw = localStorage.getItem(SESSIONS_KEY)
      if (raw) {
        const parsed = JSON.parse(raw) as ChatSession[]
        if (Array.isArray(parsed) && parsed.length) {
          sessions.value = parsed
          currentSessionId.value = parsed[parsed.length - 1].id
          return
        }
      }

      // 旧版数据迁移：平铺消息 → 单会话链式树
      const legacy = localStorage.getItem(LEGACY_KEY)
      if (legacy) {
        const parsedLegacy = JSON.parse(legacy)
        if (Array.isArray(parsedLegacy?.messages) && parsedLegacy.messages.length) {
          const branchId = uid('br')
          let parentId: string | null = null
          const migrated: ChatMessage[] = parsedLegacy.messages.map((m: any) => {
            const msg: ChatMessage = {
              id: m.id || uid('m'),
              parentId,
              branchId,
              role: m.role === 'user' ? 'user' : m.role === 'system' ? 'system' : 'assistant',
              content: m.content || '',
              timestamp: m.timestamp || Date.now(),
              thoughts: m.thoughts,
              tokenUsage: m.tokenUsage,
            }
            parentId = msg.id
            return msg
          })
          const session: ChatSession = {
            id: uid('sess'),
            title: '历史对话（已迁移）',
            messages: migrated,
            currentLeafId: migrated[migrated.length - 1].id,
            createdAt: migrated[0]?.timestamp || Date.now(),
            updatedAt: Date.now(),
          }
          sessions.value = [session]
          currentSessionId.value = session.id
          persistSessions()
          return
        }
      }
    } catch (e) {
      console.warn('[Chat] 加载本地会话失败', e)
    }
  }

  /* ---------- 会话管理 ---------- */

  function createSession(title = '新会话'): ChatSession {
    const session: ChatSession = {
      id: uid('sess'),
      title,
      messages: [],
      currentLeafId: null,
      createdAt: Date.now(),
      updatedAt: Date.now(),
    }
    sessions.value.push(session)
    currentSessionId.value = session.id
    persistSessions()
    return session
  }

  /** 确保有当前会话 */
  function ensureSession(): ChatSession {
    if (currentSession.value) return currentSession.value
    return createSession()
  }

  function switchSession(id: string) {
    if (sessions.value.some((s) => s.id === id)) {
      currentSessionId.value = id
      persistSessions()
    }
  }

  function deleteSession(id: string) {
    const idx = sessions.value.findIndex((s) => s.id === id)
    if (idx < 0) return
    sessions.value.splice(idx, 1)
    if (currentSessionId.value === id) {
      currentSessionId.value = sessions.value[sessions.value.length - 1]?.id ?? ''
    }
    persistSessions()
  }

  function setSessionTitle(id: string, title: string) {
    const s = sessions.value.find((x) => x.id === id)
    if (s) {
      s.title = title
      s.updatedAt = Date.now()
      persistSessions()
    }
  }

  /** 用首条用户消息自动命名会话 */
  function autoTitle(session: ChatSession) {
    if (session.title && session.title !== '新会话') return
    const firstUser = session.messages.find((m) => m.role === 'user' && m.content)
    if (firstUser) session.title = firstUser.content.slice(0, 24)
  }

  /* ---------- 消息操作 ---------- */

  function findMessage(messageId: string): ChatMessage | undefined {
    return currentSession.value?.messages.find((m) => m.id === messageId)
  }

  /**
   * 添加消息。
   * 不传 parentId 时，接在当前叶子后面。
   */
  function addMessage(
    msg: Omit<ChatMessage, 'id' | 'timestamp' | 'parentId' | 'branchId'> & {
      id?: string
      parentId?: string | null
      branchId?: string
    },
  ): ChatMessage {
    const session = ensureSession()
    const parentId = msg.parentId !== undefined ? msg.parentId : session.currentLeafId
    const parent = parentId ? session.messages.find((m) => m.id === parentId) : null

    const message: ChatMessage = {
      id: msg.id ?? uid('m'),
      parentId,
      branchId: msg.branchId ?? parent?.branchId ?? uid('br'),
      role: msg.role,
      content: msg.content,
      timestamp: Date.now(),
      thoughts: msg.thoughts,
      tokenUsage: msg.tokenUsage,
      artifactIds: msg.artifactIds,
    }

    session.messages.push(message)
    session.currentLeafId = message.id
    session.updatedAt = Date.now()
    autoTitle(session)
    persistSessions()
    return message
  }

  /** 更新消息内容 */
  function updateMessage(messageId: string, patch: Partial<ChatMessage>) {
    const m = findMessage(messageId)
    if (!m) return
    Object.assign(m, patch)
    persistSessions()
  }

  /**
   * 从某条消息处分叉：新建一条 user 消息挂在该节点下。
   * 若该节点原本已有子节点，就会形成分支。
   */
  function forkFrom(messageId: string, content: string): ChatMessage {
    const session = ensureSession()
    if (!session.messages.some((m) => m.id === messageId)) {
      throw new Error(`forkFrom: 消息 ${messageId} 不存在`)
    }
    return addMessage({
      role: 'user',
      content,
      parentId: messageId,
      branchId: uid('br'),
    })
  }

  /**
   * 编辑某条消息：在同一个父节点下插入兄弟节点，形成新分支。
   */
  function editMessage(messageId: string, content: string): ChatMessage {
    const session = ensureSession()
    const target = session.messages.find((m) => m.id === messageId)
    if (!target) throw new Error(`editMessage: 消息 ${messageId} 不存在`)
    return addMessage({
      role: 'user',
      content,
      parentId: target.parentId,
      branchId: uid('br'),
    })
  }

  /**
   * 重新生成：在 assistant 消息的父节点下插入一个空的兄弟消息占位。
   */
  function addAssistantSibling(assistantId: string): ChatMessage {
    const session = ensureSession()
    const target = session.messages.find((m) => m.id === assistantId)
    if (!target) throw new Error(`addAssistantSibling: 消息 ${assistantId} 不存在`)
    return addMessage({
      role: 'assistant',
      content: '',
      parentId: target.parentId,
      branchId: uid('br'),
    })
  }

  /* ---------- 分支导航 ---------- */

  function childrenOf(messageId: string | null): ChatMessage[] {
    const session = currentSession.value
    if (!session) return []
    return session.messages
      .filter((m) => m.parentId === messageId)
      .sort((a, b) => a.timestamp - b.timestamp)
  }

  /**
   * 某条消息的分支信息。
   * 返回 null 表示该消息只有一个后续（不显示分支导航）。
   */
  function branchInfo(messageId: string): { index: number; total: number } | null {
    const session = currentSession.value
    if (!session) return null
    const children = childrenOf(messageId)
    if (children.length <= 1) return null

    const path = pathTo(session, session.currentLeafId)
    const idxInPath = path.findIndex((m) => m.id === messageId)
    const nextOnPath = idxInPath >= 0 ? path[idxInPath + 1] : null
    const index = nextOnPath ? children.findIndex((c) => c.id === nextOnPath.id) : 0
    return { index: Math.max(0, index), total: children.length }
  }

  /** 在同一分支点切换到上/下一个分支 */
  function switchBranch(messageId: string, delta: number) {
    const session = currentSession.value
    if (!session) return
    const children = childrenOf(messageId)
    if (children.length <= 1) return

    const path = pathTo(session, session.currentLeafId)
    const idxInPath = path.findIndex((m) => m.id === messageId)
    const nextOnPath = idxInPath >= 0 ? path[idxInPath + 1] : null
    const cur = nextOnPath ? children.findIndex((c) => c.id === nextOnPath.id) : 0
    const target = children[((cur + delta) % children.length + children.length) % children.length]

    session.currentLeafId = deepestLeaf(session, target.id)
    session.updatedAt = Date.now()
    persistSessions()
  }

  /** 直接跳到某个叶子（分支树里点节点用） */
  function switchTo(leafId: string) {
    const session = currentSession.value
    if (!session) return
    if (!session.messages.some((m) => m.id === leafId)) return
    session.currentLeafId = leafId
    session.updatedAt = Date.now()
    persistSessions()
  }

  /* ---------- 产物 ---------- */

  function attachArtifact(messageId: string, artifact: ArtifactEnvelope) {
    artifacts.value[artifact.id] = artifact
    const m = findMessage(messageId)
    if (m) {
      const ids = m.artifactIds ?? []
      if (!ids.includes(artifact.id)) m.artifactIds = [...ids, artifact.id]
    }
    persistArtifacts()
    persistSessions()
  }

  function getArtifact(id: string): ArtifactEnvelope | undefined {
    return artifacts.value[id]
  }

  function artifactsFor(messageId: string): ArtifactEnvelope[] {
    const m = findMessage(messageId)
    if (!m?.artifactIds?.length) return []
    return m.artifactIds
      .map((id) => artifacts.value[id])
      .filter((a): a is ArtifactEnvelope => !!a)
  }

  /* ---------- 兼容旧 API ---------- */

  /** 清空当前会话的消息（保留会话本身） */
  function clearMessages() {
    const session = ensureSession()
    session.messages = []
    session.currentLeafId = null
    session.updatedAt = Date.now()
    persistSessions()
  }

  function getConversationHistory(count: number = 20): ChatMessage[] {
    return messages.value.slice(-count)
  }

  function addGreeting() {
    // 预留：欢迎语
  }

  /** @deprecated 消息树模式下不做压缩，保留签名兼容 */
  async function summarizeConversation(
    _llmCallback: (prompt: string) => Promise<string>,
  ): Promise<string> {
    console.warn('[ChatStore] summarizeConversation 在消息树模式下不再压缩历史')
    return ''
  }

  /* ---------- 显示控制 ---------- */

  function toggle() {
    visible.value = !visible.value
    if (visible.value && messages.value.length === 0) {
      injectMarketContext()
      addGreeting()
    }
  }

  function setVisible(val: boolean) {
    visible.value = val
    if (val && messages.value.length === 0) {
      injectMarketContext()
      addGreeting()
    }
  }

  /* ---------- 行情背景（与旧版一致） ---------- */

  async function injectMarketContext() {
    try {
      const quoteStore = useQuoteStore()
      const accountStore = useAccountStore()

      quoteStore.subscribe('SH000001')
      quoteStore.subscribe('SH000300')
      await quoteStore.fetchAll()

      const shIndex = quoteStore.getQuote('SH000001')
      const hs300 = quoteStore.getQuote('SH000300')

      let sectorText = ''
      try {
        const sectors = await fetchSectorQuotes()
        const { totalUp, totalDown } = calculateTotalAdvanceDecline(sectors)
        const sorted = [...sectors].sort((a, b) => b.change_pct - a.change_pct)
        const topGainers = sorted.slice(-3).reverse()
        const topLosers = sorted.slice(0, 3)

        sectorText = `\n• 市场广度：上涨 ${totalUp} 家，下跌 ${totalDown} 家`
        if (topGainers.length > 0) {
          sectorText += `\n• 领涨板块：${topGainers.map((s) => `${s.name}(+${s.change_pct.toFixed(2)}%)`).join('、')}`
        }
        if (topLosers.length > 0) {
          sectorText += `\n• 领跌板块：${topLosers.map((s) => `${s.name}(${s.change_pct.toFixed(2)}%)`).join('、')}`
        }
      } catch (e) {
        console.warn('[Chat] 获取板块数据失败', e)
      }

      let accountRiskText = ''
      if (accountStore.maxDrawDown !== 0 || accountStore.sharpRatio !== 0 || accountStore.todayPL !== 0) {
        accountRiskText = `
【账户风险状况】
• 最大回撤：${accountStore.maxDrawDown.toFixed(2)}%
• 夏普比率：${accountStore.sharpRatio.toFixed(2)}
• 今日盈亏：¥${accountStore.todayPL.toLocaleString()}`
      }

      const today = new Date()
      const dateStr = `${today.getFullYear()}-${String(today.getMonth() + 1).padStart(2, '0')}-${String(today.getDate()).padStart(2, '0')}`

      marketContext.value = `【当前市场背景 ${dateStr}】
${shIndex.lastPrice > 0 ? `上证指数：${shIndex.lastPrice.toFixed(2)} 点 (${shIndex.changePct >= 0 ? '+' : ''}${shIndex.changePct.toFixed(2)}%)` : ''}
${hs300.lastPrice > 0 ? `沪深300：${hs300.lastPrice.toFixed(2)} 点 (${hs300.changePct >= 0 ? '+' : ''}${hs300.changePct.toFixed(2)}%)` : ''}
${sectorText}
${accountRiskText}

请在回答时参考以上市场数据和风险指标，提供与当前市场环境相关的建议和分析。`.trim()
    } catch (e) {
      console.warn('[Chat] 注入行情数据失败', e)
      marketContext.value = ''
    }
  }

  /* ---------- 初始化 ---------- */

  loadFromStorage()

  return {
    // 状态
    visible,
    isLoading,
    marketContext,
    useLangGraph,

    // 会话
    sessions,
    currentSessionId,
    currentSession,
    createSession,
    ensureSession,
    switchSession,
    deleteSession,
    setSessionTitle,

    // 消息树
    messages,
    allMessages,
    currentLeafId,
    historyMessages,
    addMessage,
    updateMessage,
    forkFrom,
    editMessage,
    addAssistantSibling,
    clearMessages,
    getConversationHistory,
    addGreeting,
    summarizeConversation,

    // 分支
    childrenOf,
    branchInfo,
    switchBranch,
    switchTo,

    // 产物
    artifacts,
    attachArtifact,
    getArtifact,
    artifactsFor,

    // 显示
    toggle,
    setVisible,
    injectMarketContext,
  }
})
