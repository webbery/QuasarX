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
      </span>

      <button type="button" class="icon-btn" data-tooltip="新建会话" @click="onNewSession">
        <i class="fas fa-plus"></i>
      </button>
      <button type="button" class="icon-btn" data-tooltip="清空当前会话" @click="onClear">
        <i class="fas fa-trash"></i>
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

    <!-- 输入区 -->
    <div class="composer">
      <div class="composer-inner">
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
            <button
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
    </div>

    <!-- 状态栏 -->
    <div class="status-bar">
      <span><span class="dot" :class="{ busy: isLoading }"></span>{{ currentAgent }}</span>
      <span class="sep"></span>
      <span>{{ chatStore.messages.length }} 条消息</span>
      <span class="sep"></span>
      <span>{{ artifactTotal }} 个产物</span>
      <span class="grow"></span>
      <span v-if="branchNodes > 1">{{ branchNodes }} 个分支点</span>
      <span v-if="lastError" class="status-error" :data-tooltip="lastError">
        <i class="fas fa-circle-exclamation"></i> 最近一次出错
      </span>
    </div>

    <!-- 分支/会话浮层 -->
    <AgentBranchTree v-if="branchOpen" @close="branchOpen = false" />
  </div>
</template>

<script setup lang="ts">
import { computed, nextTick, onMounted, provide, ref } from 'vue'
import { useChatStore } from '@/stores/chatStore'
import { useAgentSession } from '@/composables/useAgentSession'
import { ensureAgentActionsRegistered } from '@/lib/agent/agentActions'
import { AGENT_SESSION_KEY } from './sessionContext'
import { registerAgentArtifacts } from './registerAgentArtifacts'
import AgentConversation from './AgentConversation.vue'
import AgentWorkflowTimeline from './AgentWorkflowTimeline.vue'
import AgentBranchTree from './AgentBranchTree.vue'

const chatStore = useChatStore()
const session = useAgentSession()

// 解构出来才能在模板里自动解包 ref
const { loading: isLoading, currentAgent, lastError, events } = session

provide(AGENT_SESSION_KEY, session)

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

function scrollToBottom() {
  conversationRef.value?.scrollToBottom?.()
}

onMounted(() => {
  // 注册产物渲染器与动作处理器（幂等）
  registerAgentArtifacts()
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

.composer-inner {
  max-width: 812px;
  margin: 0 auto;
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
  font-size: 11px;
  color: #6f7b8a;
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

/* 状态栏 */
.status-bar {
  display: flex;
  align-items: center;
  gap: 14px;
  padding: 5px 14px;
  border-top: 1px solid var(--border, #2a3449);
  background: var(--dark-bg, #121826);
  font-size: 11px;
  color: #6f7b8a;
}

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
}
</style>
