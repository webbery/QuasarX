<template>
  <Teleport to="body">
    <div v-if="chatStore.visible" class="chat-box" :style="containerStyle">
      <div class="resize-handle resize-handle-left" @mousedown="startResizeLeft"></div>

      <div class="chat-header" @mousedown="startDrag">
        <div class="chat-title">
          <i class="fas fa-robot"></i>
          <span>QuasarX AI 助手</span>
        </div>
        <div class="chat-actions">
          <button @click="handleClear" title="清空聊天" class="action-btn">
            <i class="fas fa-trash"></i>
          </button>
        </div>
      </div>

      <!-- Tab 切换 -->
      <div class="tab-bar">
        <div class="tab-item" :class="{ active: activeTab === 'chat' }" @click="activeTab = 'chat'">
          <i class="fas fa-comments"></i><span>对话</span>
        </div>
        <div class="tab-item" :class="{ active: activeTab === 'agent' }" @click="activeTab = 'agent'">
          <i class="fas fa-project-diagram"></i><span>Agent 工作流</span>
          <span v-if="agentEvents.length" class="tab-badge">{{ agentEvents.length }}</span>
        </div>
      </div>

      <!-- 对话 Tab -->
      <div v-if="activeTab === 'chat'" class="tab-content message-list" ref="messageListRef">
        <template v-for="msg in chatStore.messages" :key="msg.id">
          <div v-if="msg.content" class="message" :class="msg.role">
            <div class="message-avatar">
              <i :class="msg.role === 'user' ? 'fas fa-user' : 'fas fa-robot'"></i>
            </div>
            <div class="message-body">
              <div class="message-content">
                <div class="message-text" :class="{ 'markdown-body': msg.role === 'assistant' }"
                  v-html="msg.role === 'assistant' ? renderMarkdown(msg.content) : msg.content"></div>
                <div class="message-meta">
                  <span class="message-time">{{ formatTime(msg.timestamp) }}</span>
                  <span v-if="msg.role === 'assistant' && msg.tokenUsage" class="token-usage"
                    :title="`Prompt: ${msg.tokenUsage.promptTokens}, Completion: ${msg.tokenUsage.completionTokens}`">
                    🧮 {{ msg.tokenUsage.totalTokens }} tokens
                  </span>
                </div>
              </div>
              <div v-if="msg.thoughts?.length" class="thoughts-wrapper">
                <a @click="toggleThoughts(msg.id)" class="thought-toggle-link">
                  <i :class="expandedThoughts.has(msg.id) ? 'fas fa-chevron-up' : 'fas fa-chevron-down'"></i>
                  {{ expandedThoughts.has(msg.id) ? '收起思考' : `展开思考 (${msg.thoughts.length}步)` }}
                </a>
                <transition name="thought-slide">
                  <div v-if="expandedThoughts.has(msg.id)" class="thought-steps-list">
                    <div v-for="(step, index) in msg.thoughts" :key="step.id" class="thought-step">
                      <span class="thought-step-header">
                        <span class="thought-icon">💭</span>
                        <span class="thought-step-number">第{{ index + 1 }}步</span>
                      </span>
                      <span class="thought-content">{{ step.content }}</span>
                    </div>
                  </div>
                </transition>
              </div>
            </div>
          </div>
        </template>

        <div v-if="chatStore.isLoading" class="message assistant">
          <div class="message-avatar"><i class="fas fa-robot"></i></div>
          <div class="message-body">
            <div class="message-content">
              <div class="typing-indicator"><span></span><span></span><span></span></div>
            </div>
            <div v-if="loadingThoughts?.length" class="thoughts-wrapper">
              <a @click="toggleThoughts('loading')" class="thought-toggle-link">
                <i :class="expandedThoughts.has('loading') ? 'fas fa-chevron-up' : 'fas fa-chevron-down'"></i>
                {{ expandedThoughts.has('loading') ? '收起思考' : `展开思考 (${loadingThoughts.length}步)` }}
              </a>
              <transition name="thought-slide">
                <div v-if="expandedThoughts.has('loading')" class="thought-steps-list">
                  <div v-for="(step, index) in loadingThoughts" :key="step.id" class="thought-step">
                    <span class="thought-step-header">
                      <span class="thought-icon">💭</span>
                      <span class="thought-step-number">第{{ index + 1 }}步</span>
                    </span>
                    <span class="thought-content">{{ step.content }}</span>
                  </div>
                </div>
              </transition>
            </div>
          </div>
        </div>
      </div>

      <!-- Agent 工作流 Tab -->
      <div v-if="activeTab === 'agent'" class="tab-content agent-timeline" ref="messageListRef">
        <div v-if="agentEvents.length === 0" class="empty-agent-state">
          <i class="fas fa-diagram-project"></i>
          <p>Agent 工作流将在此展示多 Agent 协作过程</p>
          <p class="hint">发送一条消息开始对话</p>
        </div>
        <div v-for="(evt, index) in agentEvents" :key="index" class="timeline-item" :class="evt.eventType">
          <div class="timeline-agent-badge" :class="evt.agent">{{ agentLabel(evt.agent) }}</div>
          <div class="timeline-content">
            <div class="timeline-event-type">{{ eventLabel(evt.eventType) }}</div>
            <div class="timeline-text">{{ evt.content }}</div>
            <div class="timeline-time">{{ formatTime(evt.timestamp) }}</div>
          </div>
        </div>
        <div v-if="chatStore.isLoading" class="timeline-item loading-indicator">
          <div class="timeline-agent-badge supervisor">
            <i class="fas fa-spinner fa-spin"></i> 处理中
          </div>
        </div>
      </div>

      <!-- 输入框 -->
      <div class="input-area">
        <textarea v-model="inputText" @keydown.enter.exact.prevent="sendMessage" placeholder="输入问题..."
          :disabled="chatStore.isLoading" rows="3"></textarea>
        <button @click="sendMessage" :disabled="!inputText.trim() || chatStore.isLoading" class="send-btn">
          <i class="fas fa-paper-plane"></i>
        </button>
      </div>
    </div>
  </Teleport>
</template>

<script setup lang="ts">
import { ref, computed, watch, nextTick, onMounted, onUnmounted } from 'vue'
import { useChatStore, type ThoughtStep, type TokenUsage } from '@/stores/chatStore'
import { askAI, type AskAIProgress } from '@/lib/ChatApi'
import MarkdownIt from 'markdown-it'

const md = new MarkdownIt({ html: true, breaks: true, linkify: true })
const chatStore = useChatStore()
const inputText = ref('')
const messageListRef = ref<HTMLDivElement>()

// Tab 状态
const activeTab = ref<'chat' | 'agent'>('chat')

// Agent 工作流事件
interface AgentEvent {
  agent: string;
  content: string;
  eventType: 'thought' | 'tool_call' | 'tool_result' | 'response' | 'error';
  timestamp: number;
}
const agentEvents = ref<AgentEvent[]>([])
const loadingThoughts = ref<ThoughtStep[]>([])
const expandedThoughts = ref(new Set<string>())

const agentLabels: Record<string, string> = {
  supervisor: '🧠 Supervisor', chat: '💬 Chat', strategy: '📐 Strategy',
  risk: '🛡️ Risk', portfolio: '📊 Portfolio',
}
const eventLabels: Record<string, string> = {
  thought: '💭 思考', tool_call: '🔧 调用工具', tool_result: '📥 工具返回',
  response: '✅ 回复', error: '❌ 错误',
}
function agentLabel(a: string) { return agentLabels[a] || a }
function eventLabel(t: string) { return eventLabels[t] || t }

function toggleThoughts(id: string) {
  expandedThoughts.value.has(id) ? expandedThoughts.value.delete(id) : expandedThoughts.value.add(id)
}

// IPC 事件监听
function onAgentEvent(_e: any, evt: AgentEvent) {
  agentEvents.value.push(evt)
  if (activeTab.value === 'agent' && messageListRef.value) {
    nextTick(() => { messageListRef.value!.scrollTop = messageListRef.value!.scrollHeight })
  }
}
onMounted(() => { window.ipcRenderer?.on('agent-event', onAgentEvent) })
onUnmounted(() => { window.ipcRenderer?.removeListener('agent-event', onAgentEvent) })

// 拖拽 & 调整大小
const isDragging = ref(false), dragStart = ref({ x: 0, y: 0 })
const containerPosition = ref({ right: 20, bottom: 80 })
const isResizing = ref(false), resizeStartX = ref(0), containerWidth = ref(480)
const containerStyle = computed(() => ({
  right: `${containerPosition.value.right}px`, bottom: `${containerPosition.value.bottom}px`, width: `${containerWidth.value}px`,
}))

function startDrag(e: MouseEvent) {
  isDragging.value = true; dragStart.value = { x: e.clientX, y: e.clientY }
  const mm = (ev: MouseEvent) => { if (!isDragging.value) return; containerPosition.value.right -= ev.clientX - dragStart.value.x; containerPosition.value.bottom -= ev.clientY - dragStart.value.y; dragStart.value = { x: ev.clientX, y: ev.clientY } }
  const mu = () => { isDragging.value = false; document.removeEventListener('mousemove', mm); document.removeEventListener('mouseup', mu) }
  document.addEventListener('mousemove', mm); document.addEventListener('mouseup', mu)
}

function startResizeLeft(e: MouseEvent) {
  e.preventDefault(); e.stopPropagation(); isResizing.value = true; resizeStartX.value = e.clientX
  const mm = (ev: MouseEvent) => { if (!isResizing.value) return; containerWidth.value = Math.min(Math.max(containerWidth.value - (ev.clientX - resizeStartX.value), 380), 900); resizeStartX.value = ev.clientX }
  const mu = () => { isResizing.value = false; document.removeEventListener('mousemove', mm); document.removeEventListener('mouseup', mu) }
  document.addEventListener('mousemove', mm); document.addEventListener('mouseup', mu)
}

watch(() => chatStore.messages, async () => {
  await nextTick(); if (messageListRef.value) messageListRef.value.scrollTop = messageListRef.value.scrollHeight
}, { deep: true })

function handleClear() {
  if (confirm('确定要清空聊天吗？')) {
    chatStore.clearMessages(); chatStore.addGreeting(); agentEvents.value = []
    window.ipcRenderer?.invoke('langgraph-clear-session')
  }
}

async function sendMessage() {
  const text = inputText.value.trim()
  if (!text || chatStore.isLoading) return

  chatStore.addMessage({ role: 'user', content: text })
  inputText.value = ''; chatStore.isLoading = true; agentEvents.value = []

  const assistantMsg = chatStore.addMessage({ role: 'assistant', content: '', thoughts: [], tokenUsage: undefined })
  loadingThoughts.value = []

  try {
    const useLangGraph = chatStore.useLangGraph ?? true

    if (useLangGraph && window.ipcRenderer) {
      const result = await window.ipcRenderer.invoke('langgraph-chat', text)
      chatStore.isLoading = false
      if (assistantMsg) {
        assistantMsg.content = result.answer
        assistantMsg.thoughts = agentEvents.value.filter((e: AgentEvent) => e.eventType === 'thought').map((e: AgentEvent, i: number) => ({ id: `lg-${i}`, content: e.content, timestamp: e.timestamp }))
      }
    } else {
      const result = await askAI(text, {
        context: chatStore.marketContext,
        history: chatStore.messages.slice(0, -1),
        onProgress: (p: AskAIProgress) => {
          loadingThoughts.value = [...p.thoughts]
          if (assistantMsg) assistantMsg.tokenUsage = p.tokenUsage ? { ...p.tokenUsage } : undefined
        }
      })
      chatStore.isLoading = false
      if (assistantMsg) {
        assistantMsg.content = result.answer
        assistantMsg.thoughts = result.thoughts.length > 0 ? result.thoughts : []
        assistantMsg.tokenUsage = result.tokenUsage
      }
    }
    loadingThoughts.value = []
  } catch (error) {
    chatStore.isLoading = false
    if (assistantMsg) assistantMsg.content = `抱歉，AI 服务暂时不可用：${(error as Error).message}`
    loadingThoughts.value = []
  }
}

function formatTime(ts: number) { const d = new Date(ts); return `${d.getHours().toString().padStart(2, '0')}:${d.getMinutes().toString().padStart(2, '0')}` }
function renderMarkdown(t: string) { return md.render(t) }
</script>

<style scoped>
.chat-box {
  position: fixed; z-index: 9999; width: 480px; height: 700px; max-height: calc(100vh - 100px);
  background: #1e1e2e; border-radius: 12px; box-shadow: 0 8px 32px rgba(0, 0, 0, 0.4);
  border: 1px solid rgba(255, 255, 255, 0.1); display: flex; flex-direction: column; overflow: hidden;
}
.resize-handle-left {
  position: absolute; left: 0; top: 0; bottom: 0; width: 8px; cursor: ew-resize; z-index: 10; transition: background-color 0.2s;
}
.resize-handle-left:hover { background: rgba(59, 130, 246, 0.3) }
.resize-handle-left:active { background: rgba(59, 130, 246, 0.5) }

.chat-header {
  display: flex; justify-content: space-between; align-items: center; padding: 12px 16px;
  background: linear-gradient(135deg, #3b82f6, #1d4ed8); color: white; cursor: grab; user-select: none;
}
.chat-header:active { cursor: grabbing }
.chat-title { display: flex; align-items: center; gap: 8px; font-weight: 600; font-size: 15px }
.chat-title i { font-size: 18px }
.chat-actions { display: flex; gap: 8px }
.action-btn {
  width: 28px; height: 28px; border-radius: 50%; background: rgba(255, 255, 255, 0.2);
  border: none; color: white; cursor: pointer; display: flex; align-items: center; justify-content: center; transition: all 0.2s;
}
.action-btn:hover { background: rgba(255, 255, 255, 0.3); transform: scale(1.1) }

/* Tab 栏 */
.tab-bar {
  display: flex; border-bottom: 1px solid rgba(255, 255, 255, 0.1); background: rgba(30, 30, 46, 0.95);
}
.tab-item {
  flex: 1; padding: 10px; text-align: center; cursor: pointer; font-size: 13px; color: #9ca3af;
  display: flex; align-items: center; justify-content: center; gap: 6px; transition: all 0.2s;
  border-bottom: 2px solid transparent; position: relative;
}
.tab-item:hover { color: #e5e7eb; background: rgba(59, 130, 246, 0.05) }
.tab-item.active { color: #3b82f6; border-bottom-color: #3b82f6; background: rgba(59, 130, 246, 0.08) }
.tab-item i { font-size: 14px }
.tab-badge {
  position: absolute; top: 4px; right: 12px; background: #ef4444; color: white;
  font-size: 10px; padding: 1px 5px; border-radius: 10px; min-width: 16px; text-align: center;
}
.tab-content { flex: 1; overflow: hidden }

/* 消息列表 */
.message-list {
  flex: 1; overflow-y: auto; padding: 12px; display: flex; flex-direction: column; gap: 12px;
}
.message { display: flex; gap: 8px; align-items: flex-start }
.message.user { flex-direction: row-reverse }
.message.user .message-body { align-items: flex-end }
.message-avatar {
  width: 28px; height: 28px; border-radius: 50%; display: flex; align-items: center;
  justify-content: center; flex-shrink: 0;
}
.message.assistant .message-avatar { background: linear-gradient(135deg, #3b82f6, #1d4ed8); color: white }
.message.user .message-avatar { background: rgba(255, 255, 255, 0.1); color: #9ca3af }
.message-avatar i { font-size: 14px }
.message-body { display: flex; flex-direction: column; max-width: 75%; gap: 4px }
.thoughts-wrapper { padding-left: 4px }
.thought-toggle-link {
  color: #60a5fa; cursor: pointer; text-decoration: none; font-size: 11px;
  display: inline-flex; align-items: center; gap: 4px; padding: 2px 6px; border-radius: 4px; transition: all 0.2s;
}
.thought-toggle-link:hover { background: rgba(0, 0, 0, 0.3) }
.thought-slide-enter-active, .thought-slide-leave-active { transition: all 0.25s ease-out }
.thought-slide-enter-from, .thought-slide-leave-to { opacity: 0; max-height: 0 }
.thought-steps-list {
  margin-top: 4px; padding: 10px; background: rgba(0, 0, 0, 0.15); border-radius: 6px; border-left: 3px solid #6b7280;
}
.thought-step {
  display: flex; flex-direction: column; gap: 4px; margin-bottom: 10px; padding: 8px;
  background: rgba(255, 255, 255, 0.03); border-radius: 6px;
}
.thought-step:last-child { margin-bottom: 0 }
.thought-step-header { display: flex; align-items: center; gap: 6px }
.thought-icon { flex-shrink: 0; font-size: 14px }
.thought-step-number { font-size: 11px; color: #60a5fa; font-weight: 500 }
.thought-content {
  color: #9ca3af; font-size: 12px; font-style: italic; line-height: 1.5; word-wrap: break-word; padding-left: 20px;
}
.message-text {
  padding: 8px 12px; border-radius: 12px; font-size: 13px; line-height: 1.4; word-wrap: break-word;
}
.message.assistant .message-text {
  background: rgba(59, 130, 246, 0.1); color: #e5e7eb; border: 1px solid rgba(59, 130, 246, 0.2);
}
.message-text.markdown-body { padding: 10px 14px }
.message-text.markdown-body :deep(p) { margin: 0 0 8px; line-height: 1.5 }
.message-text.markdown-body :deep(p:last-child) { margin-bottom: 0 }
.message-text.markdown-body :deep(code) {
  background: rgba(0, 0, 0, 0.3); padding: 2px 6px; border-radius: 4px; font-size: 12px;
  font-family: 'Consolas', 'Courier New', monospace;
}
.message-text.markdown-body :deep(pre) {
  background: rgba(0, 0, 0, 0.4); padding: 12px; border-radius: 8px; overflow-x: auto; margin: 8px 0;
}
.message-text.markdown-body :deep(pre code) { background: none; padding: 0; font-size: 12px; line-height: 1.4 }
.message-text.markdown-body :deep(ul), .message-text.markdown-body :deep(ol) { margin: 6px 0; padding-left: 20px }
.message-text.markdown-body :deep(li) { margin: 2px 0; font-size: 13px }
.message-text.markdown-body :deep(strong) { color: #93c5fd }
.message-text.markdown-body :deep(a) { color: #60a5fa; text-decoration: underline }
.message-text.markdown-body :deep(table) { width: 100%; border-collapse: collapse; margin: 8px 0; font-size: 12px }
.message-text.markdown-body :deep(th), .message-text.markdown-body :deep(td) {
  border: 1px solid rgba(255, 255, 255, 0.15); padding: 4px 8px; text-align: left;
}
.message-text.markdown-body :deep(th) { background: rgba(59, 130, 246, 0.15); font-weight: 600 }
.message-text.markdown-body :deep(blockquote) {
  border-left: 3px solid #3b82f6; padding-left: 10px; margin: 6px 0; color: #9ca3af;
}
.message-text.markdown-body :deep(hr) { border: none; border-top: 1px solid rgba(255, 255, 255, 0.1); margin: 10px 0 }
.message.user .message-text { background: linear-gradient(135deg, #3b82f6, #2563eb); color: white }
.message-meta { display: flex; align-items: center; gap: 8px; margin-top: 4px; padding: 0 4px }
.message-time { font-size: 10px; color: #6b7280; margin-top: 4px; padding: 0 4px }
.token-usage {
  font-size: 10px; color: #9ca3af; background: rgba(156, 163, 175, 0.1);
  padding: 2px 6px; border-radius: 4px; cursor: help; transition: all 0.2s;
}
.token-usage:hover { background: rgba(156, 163, 175, 0.2); color: #e5e7eb }
.typing-indicator {
  display: flex; gap: 4px; padding: 12px; background: rgba(59, 130, 246, 0.1);
  border-radius: 12px; border: 1px solid rgba(59, 130, 246, 0.2);
}
.typing-indicator span {
  width: 6px; height: 6px; background: #3b82f6; border-radius: 50%; animation: typing 1.4s infinite;
}
.typing-indicator span:nth-child(2) { animation-delay: 0.2s }
.typing-indicator span:nth-child(3) { animation-delay: 0.4s }
@keyframes typing {
  0%, 60%, 100% { transform: translateY(0); opacity: 0.5 }
  30% { transform: translateY(-8px); opacity: 1 }
}

/* Agent 工作流时间线 */
.agent-timeline {
  flex: 1; overflow-y: auto; padding: 12px; display: flex; flex-direction: column; gap: 8px;
}
.empty-agent-state {
  display: flex; flex-direction: column; align-items: center; justify-content: center;
  padding: 60px 20px; color: #6b7280; text-align: center;
}
.empty-agent-state i { font-size: 48px; margin-bottom: 12px; opacity: 0.3 }
.empty-agent-state p { font-size: 13px; margin: 4px 0 }
.empty-agent-state .hint { font-size: 12px; color: #4b5563 }

.timeline-item {
  padding: 10px 12px; background: rgba(255, 255, 255, 0.03); border-radius: 8px;
  border-left: 3px solid #3b82f6; animation: fadeIn 0.3s ease-out;
}
.timeline-item.thought { border-left-color: #6b7280 }
.timeline-item.tool_call { border-left-color: #f59e0b }
.timeline-item.tool_result { border-left-color: #10b981 }
.timeline-item.response { border-left-color: #3b82f6 }
.timeline-item.error { border-left-color: #ef4444; background: rgba(239, 68, 68, 0.05) }
.timeline-item.loading-indicator { border-left-color: #3b82f6; opacity: 0.7 }

@keyframes fadeIn { from { opacity: 0; transform: translateY(4px) } to { opacity: 1; transform: translateY(0) } }

.timeline-agent-badge {
  display: inline-block; font-size: 11px; font-weight: 600; padding: 2px 8px;
  border-radius: 12px; margin-bottom: 6px;
}
.timeline-agent-badge.supervisor { background: rgba(139, 92, 246, 0.2); color: #a78bfa }
.timeline-agent-badge.chat { background: rgba(59, 130, 246, 0.2); color: #60a5fa }
.timeline-agent-badge.strategy { background: rgba(16, 185, 129, 0.2); color: #34d399 }
.timeline-agent-badge.risk { background: rgba(239, 68, 68, 0.2); color: #f87171 }
.timeline-agent-badge.portfolio { background: rgba(245, 158, 11, 0.2); color: #fbbf24 }

.timeline-content { font-size: 12px; line-height: 1.5 }
.timeline-event-type { font-size: 11px; color: #9ca3af; margin-bottom: 4px }
.timeline-text {
  color: #d1d5db; word-wrap: break-word; white-space: pre-wrap;
  max-height: 120px; overflow-y: auto; padding: 4px 0;
}
.timeline-text::-webkit-scrollbar { width: 4px }
.timeline-text::-webkit-scrollbar-track { background: transparent }
.timeline-text::-webkit-scrollbar-thumb { background: rgba(255, 255, 255, 0.1); border-radius: 2px }
.timeline-time { font-size: 10px; color: #6b7280; margin-top: 4px }

/* 输入区 */
.input-area {
  display: flex; gap: 8px; padding: 12px; background: rgba(40, 40, 55, 0.9);
  border-top: 1px solid rgba(255, 255, 255, 0.1); align-items: flex-end;
}
.input-area textarea {
  flex: 1; padding: 10px 14px; background: rgba(255, 255, 255, 0.05);
  border: 1px solid rgba(255, 255, 255, 0.1); border-radius: 12px; color: #e5e7eb;
  font-size: 13px; line-height: 1.5; resize: none; outline: none; min-height: 72px;
  max-height: 180px; font-family: inherit; scrollbar-width: thin; scrollbar-color: rgba(255, 255, 255, 0.1) transparent;
}
.input-area textarea::-webkit-scrollbar { width: 6px }
.input-area textarea::-webkit-scrollbar-track { background: transparent }
.input-area textarea::-webkit-scrollbar-thumb { background: rgba(255, 255, 255, 0.1); border-radius: 3px }
.input-area textarea::-webkit-scrollbar-thumb:hover { background: rgba(255, 255, 255, 0.2) }
.input-area textarea:focus { border-color: #3b82f6 }
.input-area textarea:disabled { opacity: 0.5 }
.send-btn {
  width: 36px; height: 36px; border-radius: 50%; background: linear-gradient(135deg, #3b82f6, #1d4ed8);
  border: none; color: white; cursor: pointer; display: flex; align-items: center; justify-content: center; transition: all 0.2s;
}
.send-btn:hover:not(:disabled) { transform: scale(1.1) }
.send-btn:disabled { opacity: 0.5; cursor: not-allowed }

.message-list::-webkit-scrollbar, .agent-timeline::-webkit-scrollbar { width: 4px }
.message-list::-webkit-scrollbar-track, .agent-timeline::-webkit-scrollbar-track { background: transparent }
.message-list::-webkit-scrollbar-thumb, .agent-timeline::-webkit-scrollbar-thumb { background: rgba(255, 255, 255, 0.1); border-radius: 2px }
.message-list::-webkit-scrollbar-thumb:hover, .agent-timeline::-webkit-scrollbar-thumb:hover { background: rgba(255, 255, 255, 0.2) }
</style>
