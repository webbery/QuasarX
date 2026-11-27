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
import { ArtifactKind, type ArtifactEnvelope } from '@/lib/agent/artifacts'
import { purgeSessionMemories } from '@/lib/memory/sessionCleanup'
import {
  META_KEYS,
  deleteArtifacts,
  deleteSession as dbDeleteSession,
  getAllArtifacts,
  getAllSessions,
  getMeta,
  putArtifact,
  putSession,
  setMeta,
} from '@/lib/agent/chatDb'

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
  /** 标题来源：auto 时允许自动命名覆盖，manual 时保持用户输入 */
  titleSource?: 'auto' | 'manual'
  /** 归档时间；有值 = 已归档（默认列表不展示，但数据保留） */
  archivedAt?: number
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

/** 最多保留的活跃会话数（超出时自动归档最旧的非当前会话） */
const MAX_SESSIONS = 20
/** 最多保留的归档会话数（超出时才硬删最旧的） */
const MAX_ARCHIVED_SESSIONS = 100
/** 单个会话最多保留的消息数（超出时裁剪最老的根分支） */
const MAX_MESSAGES = 800
/**
 * 无引用（孤儿）产物上限。
 * 被消息引用的产物永久保留；孤儿按创建时间保留最近这么多个。
 */
const MAX_ORPHAN_ARTIFACTS = 200

/** 会话落盘防抖（ms） */
const SESSION_FLUSH_DEBOUNCE = 200
/** 孤儿回收防抖（ms）—— 给消息写 artifactIds 留出时间窗 */
const ARTIFACT_GC_DEBOUNCE = 5000

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
  /** IndexedDB 水合是否完成（false 时 UI 应显示加载态而非空态） */
  const hydrated = ref(false)

  /* ---------- 会话树 ---------- */
  const sessions = ref<ChatSession[]>([])
  const currentSessionId = ref('')
  const artifacts = ref<Record<string, ArtifactEnvelope>>({})

  const currentSession = computed<ChatSession | null>(
    () => sessions.value.find((s) => s.id === currentSessionId.value) ?? null,
  )

  /** 活跃会话（按最近活动倒序） */
  const activeSessions = computed<ChatSession[]>(() =>
    sessions.value
      .filter((s) => !s.archivedAt)
      .sort((a, b) => (b.updatedAt ?? 0) - (a.updatedAt ?? 0)),
  )

  /** 已归档会话（按归档时间倒序） */
  const archivedSessions = computed<ChatSession[]>(() =>
    sessions.value
      .filter((s) => !!s.archivedAt)
      .sort((a, b) => (b.archivedAt ?? 0) - (a.archivedAt ?? 0)),
  )

  /** 最近活跃的会话；没有则返回 null */
  function mostRecentActiveSession(): ChatSession | null {
    return activeSessions.value[0] ?? null
  }

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

  /* ---------- 持久化（IndexedDB） ---------- */

  /** 已标记为需要落盘的会话 id */
  const dirtySessions = new Set<string>()
  let sessionFlushTimer: ReturnType<typeof setTimeout> | null = null
  let artifactGcTimer: ReturnType<typeof setTimeout> | null = null

  /** 裁剪会话消息，并修正 currentLeafId，避免指向已被裁掉的节点 */
  function trimSession(session: ChatSession): ChatSession {
    if (session.messages.length <= MAX_MESSAGES) return session
    const keep = session.messages.slice(-MAX_MESSAGES)
    const kept = new Set(keep.map((m) => m.id))
    return {
      ...session,
      messages: keep,
      currentLeafId:
        session.currentLeafId && kept.has(session.currentLeafId)
          ? session.currentLeafId
          : (keep[keep.length - 1]?.id ?? null),
    }
  }

  /** 标记某个会话需要落盘（防抖合并写） */
  function markSessionDirty(id: string) {
    dirtySessions.add(id)
    if (sessionFlushTimer !== null) return
    sessionFlushTimer = setTimeout(() => {
      sessionFlushTimer = null
      void flushSessions()
    }, SESSION_FLUSH_DEBOUNCE)
  }

  /** 把脏会话写入 IndexedDB */
  async function flushSessions() {
    const ids = [...dirtySessions]
    dirtySessions.clear()
    for (const id of ids) {
      const session = sessions.value.find((s) => s.id === id)
      try {
        if (!session) {
          await dbDeleteSession(id)
        } else {
          await putSession(trimSession(session))
        }
      } catch (e) {
        console.warn('[Chat] 保存会话失败', id, e)
      }
    }
  }

  /** 写入单个产物（产物不可变，put 幂等） */
  async function persistArtifact(artifact: ArtifactEnvelope) {
    try {
      await putArtifact(artifact)
    } catch (e) {
      console.warn('[Chat] 保存产物失败', artifact.id, e)
    }
  }

  /** 孤儿产物回收：删掉没被任何消息引用的老产物 */
  function scheduleArtifactGC() {
    if (artifactGcTimer !== null) clearTimeout(artifactGcTimer)
    artifactGcTimer = setTimeout(() => {
      artifactGcTimer = null
      void gcArtifacts()
    }, ARTIFACT_GC_DEBOUNCE)
  }

  async function gcArtifacts() {
    const referenced = new Set<string>()
    for (const session of sessions.value) {
      for (const msg of session.messages) {
        for (const id of msg.artifactIds ?? []) referenced.add(id)
      }
    }
    const orphans = Object.values(artifacts.value)
      .filter((a) => !referenced.has(a.id))
      .sort((a, b) => a.createdAt - b.createdAt)
    if (orphans.length <= MAX_ORPHAN_ARTIFACTS) return
    const excess = orphans.slice(0, orphans.length - MAX_ORPHAN_ARTIFACTS)
    for (const a of excess) delete artifacts.value[a.id]
    try {
      await deleteArtifacts(excess.map((a) => a.id))
    } catch (e) {
      console.warn('[Chat] 回收孤儿产物失败', e)
    }
  }

  /** 记住当前会话 id */
  async function rememberCurrentSession() {
    if (!currentSessionId.value) return
    try {
      await setMeta('currentSessionId', currentSessionId.value)
    } catch (e) {
      console.warn('[Chat] 记录当前会话失败', e)
    }
  }

  /** 从旧版 localStorage 导入（仅首次启动时执行一次） */
  async function importLegacyFromLocalStorage() {
    const alreadyMigrated = await getMeta<boolean>(META_KEYS.MIGRATED_FROM_LOCAL_STORAGE)
    if (alreadyMigrated) return

    try {
      const rawSessions = localStorage.getItem(SESSIONS_KEY)
      const rawArtifacts = localStorage.getItem(ARTIFACTS_KEY)
      const legacyRaw = localStorage.getItem(LEGACY_KEY)

      const parsedSessions: ChatSession[] = rawSessions
        ? (JSON.parse(rawSessions) as ChatSession[])
        : []
      const parsedArtifacts: ArtifactEnvelope[] = rawArtifacts
        ? (JSON.parse(rawArtifacts) as ArtifactEnvelope[])
        : []

      // 更老的版本：平铺消息 → 单会话链式树
      if (!parsedSessions.length && legacyRaw) {
        const parsedLegacy = JSON.parse(legacyRaw)
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
          parsedSessions.push({
            id: uid('sess'),
            title: '历史对话（已迁移）',
            messages: migrated,
            currentLeafId: migrated[migrated.length - 1].id,
            createdAt: migrated[0]?.timestamp || Date.now(),
            updatedAt: Date.now(),
          })
        }
      }

      if (parsedSessions.length || parsedArtifacts.length) {
        for (const s of parsedSessions) await putSession(trimSession(s))
        for (const a of parsedArtifacts) await putArtifact(a)
        sessions.value = parsedSessions
        const map: Record<string, ArtifactEnvelope> = {}
        for (const a of parsedArtifacts) map[a.id] = a
        artifacts.value = map
        console.info(
          `[Chat] 已从 localStorage 迁移到 IndexedDB：${parsedSessions.length} 个会话 / ${parsedArtifacts.length} 个产物`,
        )
      }

      await setMeta(META_KEYS.MIGRATED_FROM_LOCAL_STORAGE, true)
    } catch (e) {
      console.warn('[Chat] 迁移 localStorage 数据失败', e)
    }
  }

  /** 水合：从 IndexedDB 读取会话与产物（幂等，可重复 await） */
  let hydratePromise: Promise<void> | null = null
  function hydrate(): Promise<void> {
    if (hydratePromise) return hydratePromise
    hydratePromise = (async () => {
      // 水合期间用户可能已经新建了会话 / 产生产物，先留一份快照，
      // 读完之后要合并回去，否则会把他刚发的那条消息冲掉。
      const pendingSessions = [...sessions.value]
      const pendingArtifacts = { ...artifacts.value }
      const pendingCurrentId = currentSessionId.value

      try {
        const [storedSessions, storedArtifacts] = await Promise.all([
          getAllSessions(),
          getAllArtifacts(),
        ])

        if (storedSessions.length) {
          sessions.value = mergeById(storedSessions, pendingSessions)
          const remembered = await getMeta<string>('currentSessionId')
          const valid = remembered && sessions.value.some((s) => s.id === remembered)
          currentSessionId.value = valid
            ? (remembered as string)
            : pendingCurrentId && sessions.value.some((s) => s.id === pendingCurrentId)
              ? pendingCurrentId
              : pickLatestSessionId(sessions.value)
        }

        if (storedArtifacts.length) {
          const map: Record<string, ArtifactEnvelope> = {}
          for (const a of storedArtifacts) map[a.id] = a
          // 同样把水合期间新产生的产物合并回来
          artifacts.value = { ...map, ...pendingArtifacts }
        }

        // IndexedDB 里没数据 → 首次启动，从 localStorage 导入
        if (!storedSessions.length) {
          await importLegacyFromLocalStorage()
          sessions.value = mergeById(sessions.value, pendingSessions)
          if (sessions.value.length) {
            currentSessionId.value = pickLatestSessionId(sessions.value)
          }
        }

        // 存量数据可能超过新的数量上限：活跃超限自动归档，归档超限才硬删
        await enforceSessionLimit()
      } catch (e) {
        console.warn('[Chat] 加载本地会话失败', e)
      } finally {
        hydrated.value = true
      }
    })()
    return hydratePromise
  }

  /** 合并两个列表：base 在前，extra 里 base 没有的追加在后（按 id 去重） */
  function mergeById(base: ChatSession[], extra: ChatSession[]): ChatSession[] {
    if (!extra.length) return base
    const seen = new Set(base.map((s) => s.id))
    const merged = [...base]
    for (const s of extra) {
      if (!seen.has(s.id)) {
        seen.add(s.id)
        merged.push(s)
      }
    }
    return merged
  }

  /** 取最近更新的会话 id（优先活跃会话） */
  function pickLatestSessionId(list: ChatSession[]): string {
    if (!list.length) return ''
    const pool = list.some((s) => !s.archivedAt) ? list.filter((s) => !s.archivedAt) : list
    let best = pool[0]
    for (const s of pool) {
      if ((s.updatedAt ?? 0) > (best.updatedAt ?? 0)) best = s
    }
    return best.id
  }

  /* ---------- 会话管理 ---------- */

  function createSession(title = '新会话'): ChatSession {
    const session: ChatSession = {
      id: uid('sess'),
      title,
      titleSource: 'auto',
      messages: [],
      currentLeafId: null,
      createdAt: Date.now(),
      updatedAt: Date.now(),
    }
    sessions.value.push(session)
    currentSessionId.value = session.id
    markSessionDirty(session.id)
    void rememberCurrentSession()
    void enforceSessionLimit()
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
      void rememberCurrentSession()
    }
  }

  function deleteSession(id: string) {
    const idx = sessions.value.findIndex((s) => s.id === id)
    if (idx < 0) return
    sessions.value.splice(idx, 1)
    if (currentSessionId.value === id) {
      currentSessionId.value = mostRecentActiveSession()?.id ?? ''
      void rememberCurrentSession()
    }
    // 会话没了，产物也就没人引用了 —— 让 GC 回收
    dirtySessions.delete(id)
    void dbDeleteSession(id).catch((e) => console.warn('[Chat] 删除会话失败', id, e))
    // 会话没了，该会话的记忆也一并清掉
    void purgeSessionMemories(id)
    scheduleArtifactGC()
  }

  /**
   * 重命名会话。
   * 空标题视为取消；成功后标记为手动命名，避免被首条用户消息覆盖。
   */
  function renameSession(id: string, title: string) {
    const s = sessions.value.find((x) => x.id === id)
    if (!s) return
    const next = title.replace(/\s+/g, ' ').trim().slice(0, 40)
    if (!next) return
    s.title = next
    s.titleSource = 'manual'
    markSessionDirty(id)
  }

  /** 归档会话；归档当前会话时自动切到最近活跃的会话 */
  function archiveSession(id: string) {
    const s = sessions.value.find((x) => x.id === id)
    if (!s || s.archivedAt) return
    s.archivedAt = Date.now()
    markSessionDirty(id)
    if (currentSessionId.value === id) {
      const next = mostRecentActiveSession()
      currentSessionId.value = next?.id ?? ''
      if (!next) createSession()
      void rememberCurrentSession()
    }
  }

  /** 恢复归档会话（不自动切换当前会话） */
  function unarchiveSession(id: string) {
    const s = sessions.value.find((x) => x.id === id)
    if (!s?.archivedAt) return
    delete s.archivedAt
    markSessionDirty(id)
  }

  /**
   * 会话数量治理：
   * - 活跃会话超限 → 自动归档最旧的非当前会话（不丢数据）
   * - 归档会话超限 → 才硬删最旧的（同样避开当前会话）
   */
  async function enforceSessionLimit() {
    const active = sessions.value
      .filter((s) => !s.archivedAt)
      .sort((a, b) => (a.updatedAt ?? 0) - (b.updatedAt ?? 0))
    const activeExcess = Math.max(0, active.length - MAX_SESSIONS)
    if (activeExcess > 0) {
      const candidates = active.filter((s) => s.id !== currentSessionId.value)
      for (const s of candidates.slice(0, activeExcess)) {
        if (!s.archivedAt) {
          s.archivedAt = Date.now()
          markSessionDirty(s.id)
        }
      }
    }

    const archived = sessions.value
      .filter((s) => !!s.archivedAt && s.id !== currentSessionId.value)
      .sort((a, b) => (a.archivedAt ?? 0) - (b.archivedAt ?? 0))
    const archivedExcess = Math.max(0, archived.length - MAX_ARCHIVED_SESSIONS)
    for (const s of archived.slice(0, archivedExcess)) {
      const idx = sessions.value.findIndex((x) => x.id === s.id)
      if (idx >= 0) sessions.value.splice(idx, 1)
      dirtySessions.delete(s.id)
      try {
        await dbDeleteSession(s.id)
      } catch (e) {
        console.warn('[Chat] 清理超限归档会话失败', s.id, e)
      }
      // 会话被硬删，该会话的记忆也一并清掉
      void purgeSessionMemories(s.id)
    }

    if (activeExcess > 0 || archivedExcess > 0) scheduleArtifactGC()
  }

  /** 用首条用户消息自动命名会话（只覆盖自动命名） */
  function autoTitle(session: ChatSession) {
    if (session.titleSource === 'manual') return
    if (session.title && session.title !== '新会话') return
    const firstUser = session.messages.find((m) => m.role === 'user' && m.content)
    if (firstUser) {
      session.title = firstUser.content.slice(0, 24)
      session.titleSource = 'auto'
    }
  }

  /* ---------- 消息操作 ---------- */

  /**
   * 跨会话查找消息。
   * 回合进行中用户可能切到别的会话，回复仍要写回消息原本所属的会话。
   */
  function findMessageEntry(
    messageId: string,
  ): { session: ChatSession; message: ChatMessage } | null {
    const current = currentSession.value
    if (current) {
      const message = current.messages.find((m) => m.id === messageId)
      if (message) return { session: current, message }
    }
    for (const session of sessions.value) {
      if (session === current) continue
      const message = session.messages.find((m) => m.id === messageId)
      if (message) return { session, message }
    }
    return null
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
    // 在归档会话里继续发消息 → 自动恢复为活跃会话
    if (session.archivedAt) {
      delete session.archivedAt
      markSessionDirty(session.id)
    }
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
    markSessionDirty(session.id)
    return message
  }

  /** 更新消息内容 */
  function updateMessage(messageId: string, patch: Partial<ChatMessage>) {
    const entry = findMessageEntry(messageId)
    if (!entry) return
    Object.assign(entry.message, patch)
    markSessionDirty(entry.session.id)
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
    markSessionDirty(session.id)
  }

  /** 直接跳到某个叶子（分支树里点节点用） */
  function switchTo(leafId: string) {
    const session = currentSession.value
    if (!session) return
    if (!session.messages.some((m) => m.id === leafId)) return
    session.currentLeafId = leafId
    session.updatedAt = Date.now()
    markSessionDirty(session.id)
  }

  /**
   * 查看某个节点所在的分支：切到该节点子树的最深叶子。
   * 与 switchTo 的区别是「不会把路径截断在中间节点」，用于分支树点击。
   */
  function viewBranchAt(messageId: string) {
    const session = currentSession.value
    if (!session) return
    if (!session.messages.some((m) => m.id === messageId)) return
    session.currentLeafId = deepestLeaf(session, messageId)
    session.updatedAt = Date.now()
    markSessionDirty(session.id)
  }

  /* ---------- 产物 ---------- */

  /**
   * UI 产物的去重键：同一个绑定的产物只留一张卡。
   *
   * 服务端对重复提交复用同一个 job_id，绑定字段因此完全相同 —— 不去重的话
   * 一次下载会叠出两张一模一样的进度卡。
   */
  function uiDedupKey(artifact: ArtifactEnvelope): string | null {
    if (artifact.kind !== ArtifactKind.Ui) return null
    const binding = (
      artifact.payload as {
        binding?: { kind?: string; event?: string; match?: Record<string, string>; url?: string }
      }
    )?.binding
    if (!binding) return null
    // 轮询绑定：url 里带 job_id，同一个任务就是同一张卡
    if (binding.kind === 'poll') return binding.url ? `poll|${binding.url}` : null
    if (binding.kind !== 'sse' || !binding.event) return null
    const match = Object.entries(binding.match ?? {})
      .sort()
      .map(([k, v]) => `${k}=${v}`)
      .join(',')
    return `${binding.event}|${match}`
  }

  function attachArtifact(messageId: string, artifact: ArtifactEnvelope) {
    const entry = findMessageEntry(messageId)
    const dedupKey = uiDedupKey(artifact)
    if (dedupKey && entry) {
      const existingId = (entry.message.artifactIds ?? [])
        .map((id) => artifacts.value[id])
        .find((a) => a && uiDedupKey(a) === dedupKey)?.id
      if (existingId) {
        // 同一任务的重复提交：复用已有卡片，不叠第二张
        scheduleArtifactGC()
        return
      }
    }

    artifacts.value[artifact.id] = artifact
    if (entry) {
      const ids = entry.message.artifactIds ?? []
      if (!ids.includes(artifact.id)) entry.message.artifactIds = [...ids, artifact.id]
      markSessionDirty(entry.session.id)
    }
    // 先落产物，再排队 GC —— GC 会看 artifactIds 引用，顺序不能反
    void persistArtifact(artifact)
    scheduleArtifactGC()
  }

  function getArtifact(id: string): ArtifactEnvelope | undefined {
    return artifacts.value[id]
  }

  /**
   * 把产物数据写回并落盘。
   *
   * SSE 进度更新后调用：reducer 是纯函数，只改组件内的 ref，不写回产物的话，
   * 存进 IndexedDB 的一直是初始快照 —— 重载页面卡片又从 running 复活。
   */
  function updateArtifactData(id: string, data: Record<string, unknown>) {
    const artifact = artifacts.value[id]
    if (!artifact) return
    const payload = artifact.payload as { data?: Record<string, unknown> }
    if (!payload || typeof payload !== 'object' || !('data' in payload)) return
    artifacts.value[id] = { ...artifact, payload: { ...payload, data } }
    void persistArtifact(artifacts.value[id])
  }

  function artifactsFor(messageId: string): ArtifactEnvelope[] {
    const entry = findMessageEntry(messageId)
    if (!entry?.message.artifactIds?.length) return []
    return entry.message.artifactIds
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
    markSessionDirty(session.id)
    // 消息清空后原产物无人引用，交给 GC
    scheduleArtifactGC()
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

  // 从 IndexedDB 水合（异步，UI 用 hydrated 区分加载态与空态）
  void hydrate()

  return {
    // 状态
    visible,
    isLoading,
    marketContext,
    useLangGraph,
    hydrated,
    hydrate,

    // 会话
    sessions,
    activeSessions,
    archivedSessions,
    currentSessionId,
    currentSession,
    createSession,
    ensureSession,
    switchSession,
    deleteSession,
    renameSession,
    archiveSession,
    unarchiveSession,

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
    viewBranchAt,

    // 产物
    artifacts,
    attachArtifact,
    getArtifact,
    updateArtifactData,
    artifactsFor,

    // 显示
    toggle,
    setVisible,
    injectMarketContext,
  }
})
