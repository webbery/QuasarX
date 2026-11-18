<template>
  <div class="agent-panel">
    <!-- 工具条 -->
    <div class="agent-bar">
      <div class="seg" role="group" aria-label="面板视图">
        <button type="button" :class="{ active: pane === 'chat' }" @click="pane = 'chat'">
          <i class="fas fa-comments"></i>对话
        </button>
        <button type="button" :class="{ active: pane === 'workflow' }" @click="pane = 'workflow'">
          <i class="fas fa-diagram-project"></i>Agent 工作流
          <span v-if="eventCount" class="badge">{{ eventCount }}</span>
        </button>
      </div>

      <span class="grow"></span>

      <button
        type="button"
        class="tool-btn"
        :class="{ active: branchOpen }"
        data-tooltip="会话与分支"
        @click="branchOpen = !branchOpen"
      >
        <i class="fas fa-code-branch"></i>
        分支
        <span class="badge">{{ branchNodes }}</span>
      </button>

      <span class="session-title">
        <i class="fas fa-message"></i>
        {{ chatStore.currentSession?.title || '新会话' }}
        <span v-if="chatStore.currentSession?.archivedAt" class="archived-badge">已归档</span>
      </span>

      <button type="button" class="icon-btn" data-tooltip="新建会话" @click="onNewSession">
        <i class="fas fa-plus"></i>
      </button>
      <button type="button" class="icon-btn" data-tooltip="清空当前会话消息" @click="onClear">
        <i class="fas fa-eraser"></i>
      </button>
      <button
        v-if="chatStore.currentSession?.archivedAt"
        type="button"
        class="icon-btn"
        data-tooltip="恢复当前会话"
        @click="onUnarchive"
      >
        <i class="fas fa-box-open"></i>
      </button>
      <button
        v-else
        type="button"
        class="icon-btn"
        data-tooltip="归档当前会话"
        @click="onArchive"
      >
        <i class="fas fa-box-archive"></i>
      </button>
    </div>

    <!-- 内容 -->
    <AgentConversation
      v-show="pane === 'chat'"
      ref="conversationRef"
      @fork="onForkRequest"
      @quick-send="onQuickSend"
    />
    <AgentWorkflowTimeline v-show="pane === 'workflow'" />

    <!-- 输入区：三列布局 —— 状态信息 | 输入框 | 占位（保持输入框居中） -->
    <div class="composer">
      <div class="composer-grid">
        <!-- 第一列：状态信息 -->
        <div class="status-side">
          <div class="status-line">
            <span class="status-item">
              <span class="dot" :class="{ busy: isLoading }"></span>{{ currentAgent }}
            </span>
            <span class="sep"></span>
            <span class="status-item">{{ chatStore.messages.length }} 条消息</span>
            <span class="sep"></span>
            <span class="status-item">{{ artifactTotal }} 个产物</span>
          </div>
          <div v-if="tokenStats.totalTokens > 0" class="status-line">
            <span
              class="status-item"
              :data-tooltip="`Prompt ${tokenStats.promptTokens} / Completion ${tokenStats.completionTokens} / Total ${tokenStats.totalTokens}`"
            >
              <i class="fas fa-coins"></i>
              {{ formatTokens(tokenStats.totalTokens) }} tok
            </span>
            <span class="sep"></span>
            <span class="status-item">入 {{ inputPct }} · 出 {{ outputPct }}</span>
          </div>
          <div v-if="branchNodes > 1 || lastError" class="status-line">
            <span v-if="branchNodes > 1">{{ branchNodes }} 个分支点</span>
            <span v-if="lastError" class="status-error" :class="{ 'is-rate-limit': lastErrorType === 'rate_limit' }">
              <i class="fas fa-circle-exclamation"></i>
              <span class="status-error-text" :title="lastError">{{ lastError }}</span>
            </span>
          </div>
        </div>

        <!-- 第二列：输入框 -->
        <div class="composer-col">
          <div v-if="forkParentId" class="fork-chip">
            <i class="fas fa-code-branch"></i>
            从「{{ forkPreview }}」继续，将创建新分支
            <button type="button" class="chip-x" data-tooltip="取消分支" @click="forkParentId = null">
              <i class="fas fa-xmark"></i>
            </button>
          </div>

          <div class="input-wrap">
            <textarea
              ref="inputRef"
              v-model="inputText"
              class="agent-input"
              :placeholder="placeholder"
              rows="2"
              @keydown.enter.exact.prevent="submit"
            ></textarea>
            <div class="row">
              <span class="hint">Enter 发送 · Shift+Enter 换行</span>
              <AgentModelSelect :disabled="isLoading" />
              <button
                v-if="isLoading"
                type="button"
                class="stop-btn"
                data-tooltip="中止请求"
                @click="session.abort"
              >
                <i class="fas fa-stop"></i>
              </button>
              <button
                v-else
                type="button"
                class="send-btn"
                :disabled="!canSend"
                data-tooltip="发送"
                @click="submit"
              >
                <i class="fas fa-arrow-up"></i>
              </button>
            </div>
          </div>
        </div>

        <!-- 第三列：占位列，使输入框保持居中 -->
        <div class="grid-spacer" aria-hidden="true"></div>
      </div>
    </div>

    <!-- 分支/会话浮层 -->
    <AgentBranchTree v-if="branchOpen" @close="branchOpen = false" @jump="onJumpToMessage" />

    <!-- UI 产物放大的宿主（UiFrame 最大化时 Teleport 到这里） -->
    <div ref="overlayHostRef" class="agent-panel__overlay-host"></div>
  </div>
</template>

<script setup lang="ts">
import { computed, nextTick, onMounted, provide, ref } from 'vue'
import { useChatStore } from '@/stores/chatStore'
import { useAgentSession } from '@/composables/useAgentSession'
import { ensureAgentActionsRegistered } from '@/lib/agent/agentActions'
import { AGENT_SESSION_KEY } from './sessionContext'
import { registerAgentArtifacts } from './registerAgentArtifacts'
import { registerUiTypes } from './ui/registerUiTypes'
import { UI_OVERLAY_KEY } from './ui/overlayContext'
import AgentConversation from './AgentConversation.vue'
import AgentWorkflowTimeline from './AgentWorkflowTimeline.vue'
import AgentBranchTree from './AgentBranchTree.vue'
import AgentModelSelect from './AgentModelSelect.vue'

const chatStore = useChatStore()
const session = useAgentSession()

// 解构出来才能在模板里自动解包 ref
const { loading: isLoading, currentAgent, lastError, lastErrorType, events, tokenUsage } = session

provide(AGENT_SESSION_KEY, session)

/** UI 产物放大的宿主元素 */
const overlayHostRef = ref<HTMLElement | null>(null)
provide(UI_OVERLAY_KEY, { host: overlayHostRef })

const pane = ref<'chat' | 'workflow'>('chat')
const branchOpen = ref(false)
const inputText = ref('')
const forkParentId = ref<string | null>(null)
const inputRef = ref<HTMLTextAreaElement | null>(null)
const conversationRef = ref<InstanceType<typeof AgentConversation> | null>(null)

const eventCount = computed(() => events.value.length)

/** 分支点数量：有多个子节点的消息数 */
const branchNodes = computed(() => {
  const counts = new Map<string | null, number>()
  for (const m of chatStore.allMessages) {
    counts.set(m.parentId, (counts.get(m.parentId) ?? 0) + 1)
  }
  let n = 0
  for (const c of counts.values()) if (c > 1) n += 1
  return n
})

/** 当前路径上的产物总数 */
const artifactTotal = computed(() =>
  chatStore.messages.reduce((sum, m) => sum + (m.artifactIds?.length ?? 0), 0),
)

/**
 * 当前路径的累计 token 用量。
 * 本轮进行中时用量还没写回消息，需要补上会话里的实时值；
 * 回合结束后实时值已随消息落库，这里不再重复累加。
 */
const tokenStats = computed(() => {
  const acc = { promptTokens: 0, completionTokens: 0, totalTokens: 0 }
  for (const m of chatStore.messages) {
    if (!m.tokenUsage) continue
    acc.promptTokens += m.tokenUsage.promptTokens
    acc.completionTokens += m.tokenUsage.completionTokens
    acc.totalTokens += m.tokenUsage.totalTokens
  }

  const leaf = chatStore.messages[chatStore.messages.length - 1]
  const live = tokenUsage.value
  if (live && !(leaf?.role === 'assistant' && leaf.tokenUsage)) {
    acc.promptTokens += live.promptTokens
    acc.completionTokens += live.completionTokens
    acc.totalTokens += live.totalTokens
  }
  return acc
})

/** token 数量的紧凑写法 */
function formatTokens(n: number): string {
  if (n >= 1000000) return `${(n / 1000000).toFixed(1)}M`
  if (n >= 1000) return `${(n / 1000).toFixed(1)}k`
  return String(n)
}

/** 输入 / 输出 token 的占比 */
const inputPct = computed(() => {
  const total = tokenStats.value.totalTokens
  return total ? `${Math.round((tokenStats.value.promptTokens / total) * 100)}%` : '0%'
})

const outputPct = computed(() => {
  const total = tokenStats.value.totalTokens
  return total ? `${Math.round((tokenStats.value.completionTokens / total) * 100)}%` : '0%'
})

const forkPreview = computed(() => {
  if (!forkParentId.value) return ''
  const msg = chatStore.allMessages.find((m) => m.id === forkParentId.value)
  const text = (msg?.content || '').replace(/\s+/g, ' ').trim()
  if (!text) return msg?.role === 'assistant' ? '该回复' : '该消息'
  return text.length > 18 ? `${text.slice(0, 18)}…` : text
})

const canSend = computed(() => !!inputText.value.trim() && !isLoading.value)

const placeholder = computed(() =>
  forkParentId.value
    ? '从该消息分支出新的对话…'
    : '问点什么…  例：把最近一次回测的收益和回撤画出来',
)

async function submit() {
  const text = inputText.value.trim()
  if (!text || isLoading.value) return

  inputText.value = ''
  const parent = forkParentId.value
  forkParentId.value = null

  if (parent) {
    await session.forkAndSend(parent, text)
  } else {
    await session.send(text)
  }

  scrollToBottom()
}

function onQuickSend(text: string) {
  inputText.value = text
  submit()
}

function onForkRequest(messageId: string) {
  forkParentId.value = messageId
  nextTick(() => inputRef.value?.focus())
}

function onNewSession() {
  chatStore.createSession()
  forkParentId.value = null
  nextTick(() => inputRef.value?.focus())
}

function onClear() {
  if (!window.confirm('清空当前会话的消息？分支结构会一并清除，原始会话仍保留为空。')) return
  chatStore.clearMessages()
}

function onArchive() {
  const sessionId = chatStore.currentSessionId
  if (!sessionId) return
  chatStore.archiveSession(sessionId)
}

function onUnarchive() {
  const sessionId = chatStore.currentSessionId
  if (!sessionId) return
  chatStore.unarchiveSession(sessionId)
}

function onJumpToMessage(messageId: string) {
  branchOpen.value = false
  pane.value = 'chat'
  conversationRef.value?.scrollToMessage?.(messageId)
}

function scrollToBottom() {
  conversationRef.value?.scrollToBottom?.()
}

onMounted(() => {
  // 注册产物渲染器、UI 类型与动作处理器（幂等）
  registerAgentArtifacts()
  registerUiTypes()
  ensureAgentActionsRegistered()
  chatStore.ensureSession()
})
</script>

<style scoped>
.agent-panel {
  position: relative;
  flex: 1;
  min-height: 0;
  display: flex;
  flex-direction: column;
  background: var(--darker-bg, #0d111c);
}

/* UI 产物放大的宿主：铺满面板但不拦截点击（由放大的 frame 自己接管） */
.agent-panel__overlay-host {
  position: absolute;
  inset: 0;
  z-index: 40;
  pointer-events: none;
}

/* 工具条 */
.agent-bar {
  display: flex;
  align-items: center;
  gap: 10px;
  padding: 8px 14px;
  border-bottom: 1px solid var(--border, #2a3449);
  background: var(--dark-bg, #121826);
  flex-wrap: wrap;
}

.seg {
  display: inline-flex;
  border: 1px solid var(--border, #2a3449);
  border-radius: 6px;
  overflow: hidden;
}

.seg button {
  border: none;
  background: transparent;
  color: #a0aec0;
  font-size: 12px;
  padding: 5px 12px;
  cursor: pointer;
  font-family: inherit;
  display: inline-flex;
  align-items: center;
  gap: 6px;
}

.seg button + button {
  border-left: 1px solid var(--border, #2a3449);
}

.seg button.active {
  background: var(--primary, #2962ff);
  color: #fff;
}

.seg button.active .badge {
  background: rgba(255, 255, 255, 0.22);
  color: #fff;
}

.grow {
  flex: 1;
}

.tool-btn {
  height: 26px;
  padding: 0 9px;
  border-radius: 6px;
  border: 1px solid var(--border, #2a3449);
  background: transparent;
  color: #a0aec0;
  font-size: 11.5px;
  cursor: pointer;
  display: inline-flex;
  align-items: center;
  gap: 6px;
  font-family: inherit;
}

.tool-btn:hover,
.tool-btn.active {
  border-color: var(--primary, #2962ff);
  color: #e0e0e0;
}

.badge {
  font-size: 10px;
  padding: 1px 6px;
  border-radius: 20px;
  background: rgba(41, 98, 255, 0.18);
  color: #2962ff;
}

.session-title {
  display: inline-flex;
  align-items: center;
  gap: 7px;
  font-size: 12.5px;
  color: #e0e0e0;
  max-width: 260px;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.session-title i {
  color: #6f7b8a;
}

.archived-badge {
  flex: 0 0 auto;
  font-size: 9.5px;
  padding: 1px 5px;
  border-radius: 3px;
  background: rgba(255, 152, 0, 0.14);
  color: #ff9800;
}

.icon-btn {
  width: 26px;
  height: 26px;
  display: inline-flex;
  align-items: center;
  justify-content: center;
  border-radius: 6px;
  border: 1px solid var(--border, #2a3449);
  background: transparent;
  color: #a0aec0;
  cursor: pointer;
  font-size: 11px;
}

.icon-btn:hover {
  color: #e0e0e0;
  border-color: var(--primary, #2962ff);
}

/* 输入区 */
.composer {
  border-top: 1px solid var(--border, #2a3449);
  background: var(--dark-bg, #121826);
  padding: 10px 14px 12px;
}

/* 三列网格：状态信息 | 输入框 | 占位 */
.composer-grid {
  display: grid;
  grid-template-columns: 1fr 812px 1fr;
  align-items: start;
  gap: 16px;
}

.composer-col {
  min-width: 0;
}

.grid-spacer {
  min-width: 0;
}

/* 面板过窄时改为单列：状态信息移到输入框上方 */
@media (max-width: 1180px) {
  .composer-grid {
    grid-template-columns: minmax(0, 1fr);
  }

  .grid-spacer {
    display: none;
  }

  .status-side {
    order: -1;
    padding-top: 0;
    padding-bottom: 6px;
  }
}

.fork-chip {
  display: inline-flex;
  align-items: center;
  gap: 7px;
  margin-bottom: 8px;
  padding: 3px 10px;
  border-radius: 20px;
  font-size: 11px;
  background: rgba(255, 152, 0, 0.12);
  border: 1px solid rgba(255, 152, 0, 0.35);
  color: #ff9800;
}

.chip-x {
  border: none;
  background: transparent;
  color: inherit;
  cursor: pointer;
  font-size: 10px;
  padding: 0 2px;
}

.input-wrap {
  border: 1px solid var(--border, #2a3449);
  border-radius: 9px;
  background: var(--darker-bg, #0d111c);
  padding: 8px 9px 7px 12px;
  transition: border-color 0.15s;
}

/* 第一列：状态信息 */
.status-side {
  justify-self: start;
  align-self: start;
  display: flex;
  flex-direction: column;
  gap: 5px;
  padding-top: 3px;
  font-size: 11px;
  color: #6f7b8a;
}

.status-line {
  display: flex;
  align-items: center;
  gap: 8px;
  white-space: nowrap;
}

.status-item {
  display: inline-flex;
  align-items: center;
}

.status-item i {
  margin-right: 4px;
  color: #6f7b8a;
}

.input-wrap:focus-within {
  border-color: var(--primary, #2962ff);
}

.agent-input {
  width: 100%;
  border: none;
  background: transparent;
  resize: none;
  outline: none;
  color: #e0e0e0;
  font-size: 13px;
  line-height: 1.55;
  font-family: inherit;
  max-height: 160px;
}

.agent-input::placeholder {
  color: #6f7b8a;
}

.row {
  display: flex;
  align-items: center;
  gap: 7px;
  margin-top: 4px;
}

.hint {
  flex: 1;
  min-width: 0;
  font-size: 11px;
  color: #6f7b8a;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.send-btn {
  width: 29px;
  height: 29px;
  border-radius: 7px;
  border: none;
  cursor: pointer;
  background: var(--primary, #2962ff);
  color: #fff;
  display: inline-flex;
  align-items: center;
  justify-content: center;
  font-size: 12px;
}

.send-btn:disabled {
  opacity: 0.45;
  cursor: default;
}

.stop-btn {
  width: 29px;
  height: 29px;
  border-radius: 7px;
  border: none;
  cursor: pointer;
  background: #ef5350;
  color: #fff;
  display: inline-flex;
  align-items: center;
  justify-content: center;
  font-size: 11px;
}

.stop-btn:hover {
  background: #d32f2f;
}

/* 状态指示点 */
.dot {
  width: 6px;
  height: 6px;
  border-radius: 50%;
  background: #00c853;
  display: inline-block;
  margin-right: 5px;
}

.dot.busy {
  background: #ff9800;
  animation: pulse 1.2s infinite;
}

@keyframes pulse {
  0%,
  100% {
    opacity: 1;
  }
  50% {
    opacity: 0.35;
  }
}

.sep {
  width: 1px;
  height: 11px;
  background: var(--border, #2a3449);
}

.status-error {
  color: #ef5350;
  display: inline-flex;
  align-items: center;
  gap: 4px;
  min-width: 0;
}

.status-error i {
  flex: 0 0 auto;
}

.status-error.is-rate-limit {
  color: #ff9800;
}

/* 具体错误文案：单行截断，完整内容看 title */
.status-error-text {
  max-width: 230px;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}
</style>
