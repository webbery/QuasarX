<template>
  <div ref="rootRef" class="branch-pop" @click.stop>
    <!-- 搜索 -->
    <div class="search-box">
      <i class="fas fa-magnifying-glass search-icon"></i>
      <input
        v-model="query"
        class="search-input"
        type="text"
        placeholder="搜索会话标题、消息、产物…"
        @keydown.esc.prevent="clearSearch"
        @keydown.enter.prevent="openFirstResult"
      />
      <button v-if="query" class="mini-btn" data-tooltip="清空搜索" @click="clearSearch">
        <i class="fas fa-xmark"></i>
      </button>
    </div>

    <!-- 搜索结果 -->
    <template v-if="debouncedQuery">
      <div class="pop-head">
        <i class="fas fa-magnifying-glass"></i>
        <span>搜索结果</span>
        <span class="spacer"></span>
        <span class="pop-hint">{{ searchHits.length }} 个会话</span>
      </div>

      <div class="search-results">
        <div
          v-for="hit in searchHits"
          :key="hit.session.id"
          class="search-hit"
          @click="onOpenResult(hit)"
        >
          <div class="hit-head">
            <span class="hit-title">{{ hit.session.title || '未命名会话' }}</span>
            <span v-if="hit.session.archivedAt" class="tag-archived">已归档</span>
            <span class="hit-count">
              {{ hit.titleMatch && !hit.total ? '标题命中' : `${hit.total} 处` }}
            </span>
          </div>
          <div
            v-for="m in hit.matches"
            :key="m.key"
            class="hit-snippet"
            @click.stop="onOpenMatch(hit, m)"
          >
            <span class="hit-role">{{ m.role === 'user' ? '我' : 'Agent' }}</span>
            <span class="hit-text">
              <template v-for="(part, i) in highlightParts(m.snippet)" :key="i">
                <mark v-if="part.hit">{{ part.text }}</mark>
                <template v-else>{{ part.text }}</template>
              </template>
            </span>
          </div>
        </div>
        <div v-if="!searchHits.length" class="session-empty">没有匹配的会话</div>
      </div>
    </template>

    <!-- 会话列表 -->
    <template v-else>
      <div class="pop-head">
        <i class="fas fa-layer-group"></i>
        <span>会话</span>
        <span class="spacer"></span>
        <button class="mini-btn" data-tooltip="新建会话" @click="onNewSession">
          <i class="fas fa-plus"></i>
        </button>
      </div>

      <div class="session-list">
        <div
          v-for="s in activeSessions"
          :key="s.id"
          class="session-row"
          :class="{ 'is-current': s.id === chatStore.currentSessionId }"
          @click="onSwitchSession(s.id)"
        >
          <template v-if="renamingId === s.id">
            <input
              :ref="setRenameInput"
              v-model="renameDraft"
              class="rename-input"
              maxlength="40"
              @click.stop
              @keydown.enter.prevent="commitRename"
              @keydown.esc.prevent="cancelRename"
              @blur="commitRename"
            />
          </template>
          <template v-else>
            <i class="fas fa-message"></i>
            <span class="session-title" @dblclick.stop="startRename(s)">
              {{ s.title || '未命名会话' }}
            </span>
            <span class="session-count">{{ s.messages.length }}</span>
            <div class="row-actions">
              <button class="mini-btn" data-tooltip="重命名" @click.stop="startRename(s)">
                <i class="fas fa-pen"></i>
              </button>
              <button class="mini-btn" data-tooltip="归档" @click.stop="onArchive(s.id)">
                <i class="fas fa-box-archive"></i>
              </button>
              <button class="mini-btn is-danger" data-tooltip="删除" @click.stop="onDelete(s.id, s.title)">
                <i class="fas fa-trash"></i>
              </button>
            </div>
          </template>
        </div>
        <div v-if="!chatStore.hydrated" class="session-empty">读取中…</div>
        <div v-else-if="activeSessions.length === 0" class="session-empty">暂无活跃会话</div>
      </div>

      <div v-if="archivedSessions.length" class="archive-section">
        <button class="archive-toggle" @click="showArchived = !showArchived">
          <i :class="showArchived ? 'fas fa-chevron-down' : 'fas fa-chevron-right'"></i>
          已归档 ({{ archivedSessions.length }})
        </button>
        <div v-if="showArchived" class="session-list is-archived">
          <div
            v-for="s in archivedSessions"
            :key="s.id"
            class="session-row"
            :class="{ 'is-current': s.id === chatStore.currentSessionId }"
            @click="onSwitchSession(s.id)"
          >
            <template v-if="renamingId === s.id">
            <input
              :ref="setRenameInput"
              v-model="renameDraft"
              class="rename-input"
              maxlength="40"
              @click.stop
              @keydown.enter.prevent="commitRename"
              @keydown.esc.prevent="cancelRename"
              @blur="commitRename"
            />
            </template>
            <template v-else>
              <i class="fas fa-box-archive"></i>
              <span class="session-title" @dblclick.stop="startRename(s)">
                {{ s.title || '未命名会话' }}
              </span>
              <span class="session-count">{{ s.messages.length }}</span>
              <div class="row-actions">
                <button class="mini-btn" data-tooltip="重命名" @click.stop="startRename(s)">
                  <i class="fas fa-pen"></i>
                </button>
                <button class="mini-btn" data-tooltip="恢复" @click.stop="onUnarchive(s.id)">
                  <i class="fas fa-box-open"></i>
                </button>
                <button class="mini-btn is-danger" data-tooltip="删除" @click.stop="onDelete(s.id, s.title)">
                  <i class="fas fa-trash"></i>
                </button>
              </div>
            </template>
          </div>
        </div>
      </div>
    </template>

    <hr class="divider" />

    <!-- 分支树 -->
    <div class="pop-head">
      <i class="fas fa-code-branch"></i>
      <span>分支</span>
      <span class="spacer"></span>
      <button
        class="mini-btn"
        :data-tooltip="expandAll ? '收起离路径分支' : '展开全部分支'"
        @click="toggleExpandAll"
      >
        <i :class="expandAll ? 'fas fa-compress' : 'fas fa-expand'"></i>
      </button>
    </div>

    <div class="tree">
      <div
        v-for="row in rows"
        :key="row.msg.id"
        class="tree-row"
        :class="{
          'is-on-path': row.onPath,
          'is-current': row.msg.id === chatStore.currentLeafId,
          'is-branch-point': row.isBranchPoint,
        }"
        :style="{ paddingLeft: 8 + row.depth * 14 + 'px' }"
        @click="onSwitch(row)"
      >
        <button
          v-if="row.canToggle"
          class="tree-caret"
          :data-tooltip="row.expanded ? '收起子分支' : '展开子分支'"
          @click.stop="toggleExpand(row.msg.id)"
        >
          <i :class="row.expanded ? 'fas fa-chevron-down' : 'fas fa-chevron-right'"></i>
        </button>
        <span v-else class="tree-caret-spacer"></span>
        <span class="tree-dot"></span>
        <span class="tree-role">{{ row.msg.role === 'user' ? '我' : 'Agent' }}</span>
        <span class="tree-text">{{ snippet(row.msg) }}</span>
        <span v-if="row.isBranchPoint" class="tree-tag">分叉</span>
        <span class="tree-time">{{ formatTime(row.msg.timestamp) }}</span>
      </div>
      <div v-if="rows.length === 0" class="tree-empty">当前会话还没有消息</div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { computed, nextTick, onMounted, onUnmounted, ref, watch } from 'vue'
import { useChatStore, type ChatMessage, type ChatSession } from '@/stores/chatStore'

const chatStore = useChatStore()

const emit = defineEmits<{
  (e: 'close'): void
  (e: 'jump', messageId: string): void
}>()

const rootRef = ref<HTMLElement | null>(null)

/** 点击面板外关闭 */
function onDocClick(e: MouseEvent) {
  const el = rootRef.value
  if (!el) return
  if (el.contains(e.target as Node)) return
  // 触发按钮自身由父组件处理，这里只处理外部点击
  const target = e.target as HTMLElement
  if (target.closest('[data-tooltip="会话与分支"]')) return
  emit('close')
}

onMounted(() => {
  // 延后绑定，避免打开浮层的那次点击立刻把它关掉
  setTimeout(() => document.addEventListener('click', onDocClick), 0)
})

onUnmounted(() => {
  document.removeEventListener('click', onDocClick)
  if (searchTimer !== null) clearTimeout(searchTimer)
})

/* ============================================================================
 * 会话列表
 * ========================================================================== */

const activeSessions = computed(() => chatStore.activeSessions)
const archivedSessions = computed(() => chatStore.archivedSessions)
const showArchived = ref(false)

/* ---------- 重命名 ---------- */

const renamingId = ref('')
const renameDraft = ref('')
let renameInputEl: HTMLInputElement | null = null

/** 同一时间只有一个重命名输入框，用函数 ref 拿到真实元素 */
function setRenameInput(el: unknown) {
  renameInputEl = (el as HTMLInputElement | null) ?? null
}

async function startRename(session: ChatSession) {
  renamingId.value = session.id
  renameDraft.value = session.title
  await nextTick()
  renameInputEl?.focus()
  renameInputEl?.select()
}

function commitRename() {
  if (!renamingId.value) return
  const id = renamingId.value
  renamingId.value = ''
  chatStore.renameSession(id, renameDraft.value)
}

function cancelRename() {
  renamingId.value = ''
}

/* ---------- 会话操作 ---------- */

function onSwitchSession(id: string) {
  chatStore.switchSession(id)
}

function onNewSession() {
  chatStore.createSession()
}

function onArchive(id: string) {
  chatStore.archiveSession(id)
}

function onUnarchive(id: string) {
  chatStore.unarchiveSession(id)
}

function onDelete(id: string, title: string) {
  const name = title || '未命名会话'
  if (!window.confirm(`确定删除会话「${name}」？所有消息和分支将一并删除。`)) return
  chatStore.deleteSession(id)
}

/* ============================================================================
 * 搜索
 * ========================================================================== */

const query = ref('')
const debouncedQuery = ref('')
let searchTimer: ReturnType<typeof setTimeout> | null = null

watch(query, (value) => {
  if (searchTimer !== null) clearTimeout(searchTimer)
  const trimmed = value.trim()
  if (!trimmed) {
    debouncedQuery.value = ''
    return
  }
  searchTimer = setTimeout(() => {
    debouncedQuery.value = trimmed
  }, 200)
})

function clearSearch() {
  query.value = ''
  debouncedQuery.value = ''
}

interface SearchMatch {
  key: string
  message: ChatMessage
  role: ChatMessage['role']
  snippet: string
}

interface SearchHit {
  session: ChatSession
  titleMatch: boolean
  total: number
  matches: SearchMatch[]
}

/** 搜索结果：标题 + 全部消息（含离路径分支）+ 产物标题 */
const searchHits = computed<SearchHit[]>(() => {
  const q = debouncedQuery.value.toLowerCase()
  if (!q) return []

  const hits: SearchHit[] = []
  for (const session of chatStore.sessions) {
    const titleMatch = (session.title || '').toLowerCase().includes(q)
    const matches: SearchMatch[] = []
    let total = 0

    for (const msg of session.messages) {
      const content = msg.content || ''
      const idx = content.toLowerCase().indexOf(q)
      if (idx >= 0) {
        total += 1
        if (matches.length < 3) {
          matches.push({
            key: `${msg.id}:content`,
            message: msg,
            role: msg.role,
            snippet: snippetAround(content, idx, q.length),
          })
        }
      }

      for (const artifactId of msg.artifactIds ?? []) {
        const artifact = chatStore.artifacts[artifactId]
        if (!artifact || !artifact.title.toLowerCase().includes(q)) continue
        total += 1
        if (matches.length < 3) {
          matches.push({
            key: `${msg.id}:${artifactId}`,
            message: msg,
            role: msg.role,
            snippet: `[产物] ${artifact.title}`,
          })
        }
      }
    }

    if (titleMatch || total > 0) {
      hits.push({ session, titleMatch, total, matches })
    }
  }

  hits.sort((a, b) => {
    if (a.titleMatch !== b.titleMatch) return a.titleMatch ? -1 : 1
    if (b.total !== a.total) return b.total - a.total
    return (b.session.updatedAt ?? 0) - (a.session.updatedAt ?? 0)
  })

  return hits.slice(0, 20)
})

function openFirstResult() {
  if (debouncedQuery.value && searchHits.value.length) {
    onOpenResult(searchHits.value[0])
  }
}

function onOpenMatch(hit: SearchHit, match: SearchMatch) {
  chatStore.switchSession(hit.session.id)
  chatStore.viewBranchAt(match.message.id)
  emit('jump', match.message.id)
  emit('close')
}

function onOpenResult(hit: SearchHit) {
  if (hit.matches.length) {
    onOpenMatch(hit, hit.matches[0])
    return
  }
  chatStore.switchSession(hit.session.id)
  emit('close')
}

/** 截取命中位置附近的文本 */
function snippetAround(text: string, index: number, length: number): string {
  const start = Math.max(0, index - 18)
  const end = Math.min(text.length, index + length + 30)
  const body = text.slice(start, end).replace(/\s+/g, ' ').trim()
  return `${start > 0 ? '…' : ''}${body}${end < text.length ? '…' : ''}`
}

/** 按查询词切分文本，用于安全高亮（不拼 HTML） */
function highlightParts(text: string): Array<{ text: string; hit: boolean }> {
  const q = debouncedQuery.value
  if (!q) return [{ text, hit: false }]

  const lower = text.toLowerCase()
  const needle = q.toLowerCase()
  const parts: Array<{ text: string; hit: boolean }> = []
  let start = 0
  let idx = lower.indexOf(needle)

  while (idx >= 0) {
    if (idx > start) parts.push({ text: text.slice(start, idx), hit: false })
    parts.push({ text: text.slice(idx, idx + q.length), hit: true })
    start = idx + q.length
    idx = lower.indexOf(needle, start)
  }
  if (start < text.length) parts.push({ text: text.slice(start), hit: false })
  return parts
}

/* ============================================================================
 * 分支树
 * ========================================================================== */

interface TreeRow {
  msg: ChatMessage
  depth: number
  onPath: boolean
  isBranchPoint: boolean
  canToggle: boolean
  expanded: boolean
}

const expandAll = ref(false)
const expandedIds = ref<Set<string>>(new Set())
const collapsedIds = ref<Set<string>>(new Set())

function toggleExpand(id: string) {
  if (expandAll.value) {
    const next = new Set(collapsedIds.value)
    if (next.has(id)) next.delete(id)
    else next.add(id)
    collapsedIds.value = next
    return
  }
  const next = new Set(expandedIds.value)
  if (next.has(id)) next.delete(id)
  else next.add(id)
  expandedIds.value = next
}

function toggleExpandAll() {
  expandAll.value = !expandAll.value
  // 切换总开关时清掉单行例外，避免状态互相打架
  expandedIds.value = new Set()
  collapsedIds.value = new Set()
}

/** 把树拍平成带缩进的行：当前路径始终展开，离路径子树按需展开 */
const rows = computed<TreeRow[]>(() => {
  const all = chatStore.allMessages
  if (!all.length) return []

  const pathIds = new Set(chatStore.messages.map((m) => m.id))
  const byParent = new Map<string | null, ChatMessage[]>()
  for (const m of all) {
    const key = m.parentId
    if (!byParent.has(key)) byParent.set(key, [])
    byParent.get(key)!.push(m)
  }
  for (const list of byParent.values()) {
    list.sort((a, b) => a.timestamp - b.timestamp)
  }

  const out: TreeRow[] = []
  const walk = (parentId: string | null, depth: number) => {
    const children = byParent.get(parentId) ?? []
    for (const msg of children) {
      const onPath = pathIds.has(msg.id)
      const childCount = byParent.get(msg.id)?.length ?? 0
      const expanded = onPath || (expandAll.value ? !collapsedIds.value.has(msg.id) : expandedIds.value.has(msg.id))
      out.push({
        msg,
        depth,
        onPath,
        isBranchPoint: childCount > 1,
        canToggle: childCount > 0 && !onPath,
        expanded,
      })
      if (childCount > 0 && expanded) walk(msg.id, depth + 1)
    }
  }
  walk(null, 0)
  return out
})

function onSwitch(row: TreeRow) {
  if (row.onPath) {
    emit('jump', row.msg.id)
    return
  }
  chatStore.viewBranchAt(row.msg.id)
  emit('jump', row.msg.id)
}

function snippet(msg: ChatMessage): string {
  const text = (msg.content || '').replace(/\s+/g, ' ').trim()
  if (!text) return msg.artifactIds?.length ? '（产物）' : '（空）'
  return text.length > 28 ? `${text.slice(0, 28)}…` : text
}

function formatTime(ts: number): string {
  const d = new Date(ts)
  return `${String(d.getHours()).padStart(2, '0')}:${String(d.getMinutes()).padStart(2, '0')}`
}
</script>

<style scoped>
.branch-pop {
  position: absolute;
  z-index: 30;
  right: 14px;
  top: 46px;
  width: 400px;
  max-height: 74vh;
  overflow-y: auto;
  padding: 12px;
  background: var(--panel-bg, #1a2236);
  border: 1px solid var(--border, #2a3449);
  border-radius: 10px;
  box-shadow: 0 14px 38px rgba(0, 0, 0, 0.55);
}

/* 搜索 */
.search-box {
  display: flex;
  align-items: center;
  gap: 7px;
  padding: 6px 8px;
  border: 1px solid var(--border, #2a3449);
  border-radius: 7px;
  background: var(--darker-bg, #0d111c);
  margin-bottom: 10px;
}

.search-icon {
  color: #6f7b8a;
  font-size: 11px;
}

.search-input {
  flex: 1;
  min-width: 0;
  border: none;
  outline: none;
  background: transparent;
  color: #e0e0e0;
  font-size: 12px;
  font-family: inherit;
}

.search-input::placeholder {
  color: #6f7b8a;
}

/* 搜索结果 */
.search-results {
  display: flex;
  flex-direction: column;
  gap: 4px;
}

.search-hit {
  padding: 7px 8px;
  border-radius: 7px;
  cursor: pointer;
}

.search-hit:hover {
  background: rgba(255, 255, 255, 0.05);
}

.hit-head {
  display: flex;
  align-items: center;
  gap: 7px;
  margin-bottom: 3px;
}

.hit-title {
  flex: 1;
  min-width: 0;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
  font-size: 12px;
  color: #e0e0e0;
}

.hit-count {
  flex: 0 0 auto;
  font-size: 10.5px;
  color: #6f7b8a;
}

.tag-archived {
  flex: 0 0 auto;
  font-size: 9.5px;
  padding: 1px 5px;
  border-radius: 3px;
  background: rgba(255, 152, 0, 0.14);
  color: #ff9800;
}

.hit-snippet {
  display: flex;
  gap: 6px;
  padding: 3px 0 3px 2px;
  font-size: 11px;
  color: #8a97a8;
  cursor: pointer;
}

.hit-snippet:hover {
  color: #c5d0df;
}

.hit-role {
  flex: 0 0 auto;
  font-size: 10px;
  opacity: 0.75;
}

.hit-text {
  flex: 1;
  min-width: 0;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.hit-text mark {
  background: rgba(255, 193, 7, 0.28);
  color: #ffd45e;
  border-radius: 2px;
  padding: 0 1px;
}

/* 会话列表 */
.pop-head {
  display: flex;
  align-items: center;
  gap: 8px;
  font-size: 12px;
  color: #e0e0e0;
  margin-bottom: 8px;
}

.pop-head i {
  color: #2962ff;
  font-size: 11px;
}

.pop-hint {
  font-size: 10.5px;
  color: #6f7b8a;
}

.spacer {
  flex: 1;
}

.divider {
  border: none;
  border-top: 1px solid rgba(74, 85, 104, 0.3);
  margin: 12px 0;
}

.session-list {
  display: flex;
  flex-direction: column;
  gap: 2px;
}

.session-list.is-archived {
  margin-top: 4px;
  padding-top: 4px;
  border-top: 1px dashed rgba(74, 85, 104, 0.25);
}

.session-row {
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 6px 8px;
  border-radius: 7px;
  font-size: 11.5px;
  color: #a0aec0;
  cursor: pointer;
}

.session-row:hover {
  background: rgba(255, 255, 255, 0.05);
  color: #e0e0e0;
}

.session-row.is-current {
  background: rgba(41, 98, 255, 0.14);
  color: #2962ff;
}

.session-title {
  flex: 1;
  min-width: 0;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.session-count {
  flex: 0 0 auto;
  font-size: 10.5px;
  color: #6f7b8a;
}

.row-actions {
  display: none;
  align-items: center;
  gap: 1px;
}

.session-row:hover .row-actions {
  display: flex;
}

.rename-input {
  flex: 1;
  min-width: 0;
  border: 1px solid #2962ff;
  border-radius: 5px;
  background: #0d111c;
  color: #e0e0e0;
  font-size: 11.5px;
  font-family: inherit;
  padding: 3px 6px;
  outline: none;
}

.archive-section {
  margin-top: 8px;
}

.archive-toggle {
  display: flex;
  align-items: center;
  gap: 6px;
  width: 100%;
  padding: 5px 8px;
  border: none;
  border-radius: 6px;
  background: transparent;
  color: #6f7b8a;
  font-size: 11px;
  font-family: inherit;
  cursor: pointer;
}

.archive-toggle:hover {
  background: rgba(255, 255, 255, 0.04);
  color: #a0aec0;
}

.session-empty,
.tree-empty {
  padding: 10px 8px;
  font-size: 11.5px;
  color: #6f7b8a;
}

/* 分支树 */
.tree {
  display: flex;
  flex-direction: column;
  gap: 1px;
}

.tree-row {
  display: flex;
  align-items: center;
  gap: 7px;
  padding: 5px 8px;
  border-radius: 6px;
  font-size: 11.5px;
  color: #6f7b8a;
  cursor: pointer;
}

.tree-row:hover {
  background: rgba(255, 255, 255, 0.05);
  color: #e0e0e0;
}

.tree-row.is-on-path {
  color: #a0aec0;
}

.tree-row.is-current {
  background: rgba(41, 98, 255, 0.16);
  color: #2962ff;
}

.tree-caret,
.tree-caret-spacer {
  flex: 0 0 14px;
  width: 14px;
  height: 14px;
}

.tree-caret {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  padding: 0;
  border: none;
  border-radius: 3px;
  background: transparent;
  color: #6f7b8a;
  font-size: 8px;
  cursor: pointer;
}

.tree-caret:hover {
  color: #e0e0e0;
}

.tree-dot {
  width: 6px;
  height: 6px;
  border-radius: 50%;
  flex: 0 0 6px;
  background: #2a3449;
}

.tree-row.is-on-path .tree-dot {
  background: #2962ff;
}

.tree-row.is-branch-point .tree-dot {
  background: #ff9800;
}

.tree-role {
  flex: 0 0 auto;
  font-size: 10.5px;
  opacity: 0.8;
}

.tree-text {
  flex: 1;
  min-width: 0;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.tree-tag {
  font-size: 9px;
  padding: 0 4px;
  border-radius: 3px;
  background: rgba(255, 152, 0, 0.16);
  color: #ff9800;
}

.tree-time {
  font-size: 10px;
  color: #6f7b8a;
  font-variant-numeric: tabular-nums;
}

.mini-btn {
  width: 20px;
  height: 20px;
  border-radius: 5px;
  border: none;
  background: transparent;
  color: #6f7b8a;
  cursor: pointer;
  font-size: 10px;
  display: inline-flex;
  align-items: center;
  justify-content: center;
}

.mini-btn:hover {
  color: #e0e0e0;
}

.mini-btn.is-danger:hover {
  color: #ef5350;
}
</style>
