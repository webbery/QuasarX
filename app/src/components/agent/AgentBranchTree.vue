<template>
  <div ref="rootRef" class="branch-pop" @click.stop>
    <!-- 会话切换 -->
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
        v-for="s in sessions"
        :key="s.id"
        class="session-row"
        :class="{ 'is-current': s.id === chatStore.currentSessionId }"
        @click="onSwitchSession(s.id)"
      >
        <i class="fas fa-message"></i>
        <span class="session-title">{{ s.title || '未命名会话' }}</span>
        <span class="session-count">{{ s.messages.length }}</span>
        <button class="mini-btn" data-tooltip="删除会话" @click.stop="onDeleteSession(s.id)">
          <i class="fas fa-xmark"></i>
        </button>
      </div>
      <div v-if="sessions.length === 0" class="session-empty">暂无会话</div>
    </div>

    <hr class="divider" />

    <!-- 分支树 -->
    <div class="pop-head">
      <i class="fas fa-code-branch"></i>
      <span>分支</span>
      <span class="spacer"></span>
      <span class="pop-hint">点击节点切换到该分支</span>
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
        @click="onSwitch(row.msg.id)"
      >
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
import { computed, onMounted, onUnmounted, ref } from 'vue'
import { useChatStore, type ChatMessage } from '@/stores/chatStore'

const chatStore = useChatStore()

const emit = defineEmits<{ (e: 'close'): void }>()

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
})

const sessions = computed(() => chatStore.sessions)

interface TreeRow {
  msg: ChatMessage
  depth: number
  onPath: boolean
  isBranchPoint: boolean
}

/** 把树拍平成带缩进的行 */
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
      out.push({
        msg,
        depth,
        onPath: pathIds.has(msg.id),
        isBranchPoint: (byParent.get(msg.id)?.length ?? 0) > 1,
      })
      walk(msg.id, depth + 1)
    }
  }
  walk(null, 0)
  return out
})

function snippet(msg: ChatMessage): string {
  const text = (msg.content || '').replace(/\s+/g, ' ').trim()
  if (!text) return msg.artifactIds?.length ? '（产物）' : '（空）'
  return text.length > 28 ? `${text.slice(0, 28)}…` : text
}

function formatTime(ts: number): string {
  const d = new Date(ts)
  return `${String(d.getHours()).padStart(2, '0')}:${String(d.getMinutes()).padStart(2, '0')}`
}

function onSwitch(leafId: string) {
  chatStore.switchTo(leafId)
}

function onSwitchSession(id: string) {
  chatStore.switchSession(id)
}

function onNewSession() {
  chatStore.createSession()
}

function onDeleteSession(id: string) {
  chatStore.deleteSession(id)
}
</script>

<style scoped>
.branch-pop {
  position: absolute;
  z-index: 30;
  right: 14px;
  top: 46px;
  width: 380px;
  max-height: 70vh;
  overflow-y: auto;
  padding: 12px;
  background: var(--panel-bg, #1a2236);
  border: 1px solid var(--border, #2a3449);
  border-radius: 10px;
  box-shadow: 0 14px 38px rgba(0, 0, 0, 0.55);
}

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
  font-size: 10.5px;
  color: #6f7b8a;
}

.session-empty,
.tree-empty {
  padding: 10px 8px;
  font-size: 11.5px;
  color: #6f7b8a;
}

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
}

.mini-btn:hover {
  color: #ef5350;
}
</style>
