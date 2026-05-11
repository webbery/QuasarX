<template>
  <Teleport to="body">
    <div
      v-if="chatStore.visible"
      class="chat-box"
      :style="containerStyle"
    >
      <!-- 左边框调整手柄 -->
      <div class="resize-handle resize-handle-left" @mousedown="startResizeLeft"></div>
      
      <!-- 头部 -->
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

      <!-- 消息列表 -->
      <div class="message-list" ref="messageListRef">
        <div
          v-for="msg in chatStore.messages"
          :key="msg.id"
          class="message"
          :class="msg.role"
        >
          <div class="message-avatar">
            <i :class="msg.role === 'user' ? 'fas fa-user' : 'fas fa-robot'"></i>
          </div>
          <div class="message-body">
            <!-- 消息气泡（正文） -->
            <div class="message-content">
              <div
                class="message-text"
                :class="{ 'markdown-body': msg.role === 'assistant' }"
                v-html="msg.role === 'assistant' ? renderMarkdown(msg.content) : msg.content"
              ></div>
              <div class="message-time">{{ formatTime(msg.timestamp) }}</div>
            </div>

            <!-- 思考步骤（气泡下方，独立区域，点击展开/收起） -->
            <div v-if="msg.thoughts?.length" class="thoughts-wrapper">
              <a @click="toggleThoughts(msg.id)" class="thought-toggle-link">
                <i :class="expandedThoughts.has(msg.id) ? 'fas fa-chevron-up' : 'fas fa-chevron-down'"></i>
                {{ expandedThoughts.has(msg.id) ? '收起思考' : `展开思考 (${msg.thoughts.length}步)` }}
              </a>
              <transition name="thought-slide">
                <div v-if="expandedThoughts.has(msg.id)" class="thought-steps-list">
                  <div v-for="step in msg.thoughts" :key="step.id" class="thought-step">
                    <span class="thought-icon">💭</span>
                    <span class="thought-content">{{ step.content }}</span>
                  </div>
                </div>
              </transition>
            </div>
          </div>
        </div>

        <!-- 加载中提示 -->
        <div v-if="chatStore.isLoading" class="message assistant">
          <div class="message-avatar">
            <i class="fas fa-robot"></i>
          </div>
          <div class="message-content">
            <div class="typing-indicator">
              <span></span>
              <span></span>
              <span></span>
            </div>
          </div>
        </div>
      </div>

      <!-- 输入框 -->
      <div class="input-area">
        <textarea
          v-model="inputText"
          @keydown.enter.exact.prevent="sendMessage"
          placeholder="输入问题..."
          :disabled="chatStore.isLoading"
          rows="3"
        ></textarea>
        <button
          @click="sendMessage"
          :disabled="!inputText.trim() || chatStore.isLoading"
          class="send-btn"
        >
          <i class="fas fa-paper-plane"></i>
        </button>
      </div>
    </div>
  </Teleport>
</template>

<script setup lang="ts">
import { ref, computed, watch, nextTick } from 'vue'
import { useChatStore, type ThoughtStep } from '@/stores/chatStore'
import { askAI } from '@/lib/ChatApi'
import MarkdownIt from 'markdown-it'

const md = new MarkdownIt({ html: true, breaks: true, linkify: true })

const chatStore = useChatStore()
const inputText = ref('')
const messageListRef = ref<HTMLDivElement>()

// 思考展开/收缩状态
const expandedThoughts = ref(new Set<string>())

function toggleThoughts(messageId: string) {
  if (expandedThoughts.value.has(messageId)) {
    expandedThoughts.value.delete(messageId)
  } else {
    expandedThoughts.value.add(messageId)
  }
}

// 拖拽相关
const isDragging = ref(false)
const dragStart = ref({ x: 0, y: 0 })
const containerPosition = ref({ right: 20, bottom: 80 })

// 调整大小相关
const isResizing = ref(false)
const resizeStartX = ref(0)
const containerWidth = ref(480) // 默认宽度

const containerStyle = computed(() => ({
  right: `${containerPosition.value.right}px`,
  bottom: `${containerPosition.value.bottom}px`,
  width: `${containerWidth.value}px`,
}))

function startDrag(e: MouseEvent) {
  isDragging.value = true
  dragStart.value = { x: e.clientX, y: e.clientY }

  const onMouseMove = (moveEvent: MouseEvent) => {
    if (!isDragging.value) return
    const dx = moveEvent.clientX - dragStart.value.x
    const dy = moveEvent.clientY - dragStart.value.y
    containerPosition.value.right -= dx
    containerPosition.value.bottom -= dy
    dragStart.value = { x: moveEvent.clientX, y: moveEvent.clientY }
  }

  const onMouseUp = () => {
    isDragging.value = false
    document.removeEventListener('mousemove', onMouseMove)
    document.removeEventListener('mouseup', onMouseUp)
  }

  document.addEventListener('mousemove', onMouseMove)
  document.addEventListener('mouseup', onMouseUp)
}

// 开始调整大小（左边框）
function startResizeLeft(e: MouseEvent) {
  e.preventDefault()
  e.stopPropagation()
  isResizing.value = true
  resizeStartX.value = e.clientX

  const onMouseMove = (moveEvent: MouseEvent) => {
    if (!isResizing.value) return
    
    const dx = moveEvent.clientX - resizeStartX.value
    // 向左拖动（dx 为负）时增加宽度，向右拖动（dx 为正）时减少宽度
    const newWidth = containerWidth.value - dx
    
    // 限制最小宽度 380px，最大宽度 900px
    containerWidth.value = Math.min(Math.max(newWidth, 380), 900)
    resizeStartX.value = moveEvent.clientX
  }

  const onMouseUp = () => {
    isResizing.value = false
    document.removeEventListener('mousemove', onMouseMove)
    document.removeEventListener('mouseup', onMouseUp)
  }

  document.addEventListener('mousemove', onMouseMove)
  document.addEventListener('mouseup', onMouseUp)
}

// 自动滚动到底部
watch(() => chatStore.messages, async () => {
  await nextTick()
  if (messageListRef.value) {
    messageListRef.value.scrollTop = messageListRef.value.scrollHeight
  }
}, { deep: true })

// 清空消息
function handleClear() {
  if (confirm('确定要清空聊天记录吗？')) {
    chatStore.clearMessages()
    chatStore.addGreeting()
  }
}

// 发送消息
async function sendMessage() {
  const text = inputText.value.trim()
  if (!text || chatStore.isLoading) return

  // 添加用户消息
  chatStore.addMessage({
    role: 'user',
    content: text,
  })

  inputText.value = ''
  chatStore.isLoading = true

  try {
    // 调用 AI，注入行情上下文和对话历史
    const result = await askAI(text, {
      context: chatStore.marketContext,
      history: chatStore.messages.slice(0, -1), // 传递除当前消息外的所有历史
    })

    chatStore.isLoading = false

    // 添加 AI 回复
    chatStore.addMessage({
      role: 'assistant',
      content: result.answer,
      thoughts: result.thoughts.length > 0 ? result.thoughts : undefined,
    })
  } catch (error) {
    chatStore.isLoading = false
    console.error('[Chat] 发送消息失败:', error)
  }
}

// 格式化时间
function formatTime(timestamp: number): string {
  const date = new Date(timestamp)
  const hours = date.getHours().toString().padStart(2, '0')
  const minutes = date.getMinutes().toString().padStart(2, '0')
  return `${hours}:${minutes}`
}

// 渲染 markdown（仅用于助手消息）
function renderMarkdown(text: string): string {
  return md.render(text)
}
</script>

<style scoped>
.chat-box {
  position: fixed;
  z-index: 9999;
  width: 480px;
  height: 700px;
  max-height: calc(100vh - 100px);
  background: #1e1e2e;
  border-radius: 12px;
  box-shadow: 0 8px 32px rgba(0, 0, 0, 0.4);
  border: 1px solid rgba(255, 255, 255, 0.1);
  display: flex;
  flex-direction: column;
  overflow: hidden;
}

/* 左边框调整手柄 */
.resize-handle-left {
  position: absolute;
  left: 0;
  top: 0;
  bottom: 0;
  width: 8px;
  cursor: ew-resize;
  z-index: 10;
  transition: background-color 0.2s;
}

.resize-handle-left:hover {
  background: rgba(59, 130, 246, 0.3);
}

.resize-handle-left:active {
  background: rgba(59, 130, 246, 0.5);
}

.chat-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 12px 16px;
  background: linear-gradient(135deg, #3b82f6, #1d4ed8);
  color: white;
  cursor: grab;
  user-select: none;
}

.chat-header:active {
  cursor: grabbing;
}

.chat-title {
  display: flex;
  align-items: center;
  gap: 8px;
  font-weight: 600;
  font-size: 15px;
}

.chat-title i {
  font-size: 18px;
}

.chat-actions {
  display: flex;
  gap: 8px;
}

.action-btn {
  width: 28px;
  height: 28px;
  border-radius: 50%;
  background: rgba(255, 255, 255, 0.2);
  border: none;
  color: white;
  cursor: pointer;
  display: flex;
  align-items: center;
  justify-content: center;
  transition: all 0.2s;
}

.action-btn:hover {
  background: rgba(255, 255, 255, 0.3);
  transform: scale(1.1);
}

.message-list {
  flex: 1;
  overflow-y: auto;
  padding: 12px;
  display: flex;
  flex-direction: column;
  gap: 12px;
}

.message {
  display: flex;
  gap: 8px;
  align-items: flex-start;
}

.message.user {
  flex-direction: row-reverse;
}

.message.user .message-body {
  align-items: flex-end;
}

.message-avatar {
  width: 28px;
  height: 28px;
  border-radius: 50%;
  display: flex;
  align-items: center;
  justify-content: center;
  flex-shrink: 0;
}

.message.assistant .message-avatar {
  background: linear-gradient(135deg, #3b82f6, #1d4ed8);
  color: white;
}

.message.user .message-avatar {
  background: rgba(255, 255, 255, 0.1);
  color: #9ca3af;
}

.message-avatar i {
  font-size: 14px;
}

.message-body {
  display: flex;
  flex-direction: column;
  max-width: 75%;
  gap: 4px;
}

.message-content {
  /* 正文气泡 */
}

.thoughts-wrapper {
  padding-left: 4px;
}

.thought-toggle-link {
  color: #60a5fa;
  cursor: pointer;
  text-decoration: none;
  font-size: 11px;
  display: inline-flex;
  align-items: center;
  gap: 4px;
  padding: 2px 6px;
  border-radius: 4px;
  transition: all 0.2s;
}

.thought-toggle-link:hover {
  background: rgba(0, 0, 0, 0.3);
}

.thought-toggle-link i {
  font-size: 10px;
  transition: transform 0.2s;
}

/* 思考步骤滑入动画 */
.thought-slide-enter-active,
.thought-slide-leave-active {
  transition: all 0.25s ease-out;
}

.thought-slide-enter-from {
  opacity: 0;
  max-height: 0;
}

.thought-slide-leave-to {
  opacity: 0;
  max-height: 0;
}

.thought-steps-list {
  margin-top: 4px;
  padding: 10px;
  background: rgba(0, 0, 0, 0.15);
  border-radius: 6px;
  border-left: 3px solid #6b7280;
}

.thought-step {
  display: flex;
  gap: 8px;
  margin-bottom: 8px;
  align-items: flex-start;
}

.thought-step:last-child {
  margin-bottom: 0;
}

.thought-icon {
  flex-shrink: 0;
  font-size: 14px;
  margin-top: 2px;
}

.thought-content {
  color: #9ca3af;
  font-size: 12px;
  font-style: italic;
  line-height: 1.5;
  word-wrap: break-word;
}

.message-text {
  padding: 8px 12px;
  border-radius: 12px;
  font-size: 13px;
  line-height: 1.4;
  word-wrap: break-word;
}

.message.assistant .message-text {
  background: rgba(59, 130, 246, 0.1);
  color: #e5e7eb;
  border: 1px solid rgba(59, 130, 246, 0.2);
}

/* Markdown 渲染样式（仅助手消息） */
.message-text.markdown-body {
  padding: 10px 14px;
}

.message-text.markdown-body :deep(p) {
  margin: 0 0 8px;
  line-height: 1.5;
}

.message-text.markdown-body :deep(p:last-child) {
  margin-bottom: 0;
}

.message-text.markdown-body :deep(code) {
  background: rgba(0, 0, 0, 0.3);
  padding: 2px 6px;
  border-radius: 4px;
  font-size: 12px;
  font-family: 'Consolas', 'Courier New', monospace;
}

.message-text.markdown-body :deep(pre) {
  background: rgba(0, 0, 0, 0.4);
  padding: 12px;
  border-radius: 8px;
  overflow-x: auto;
  margin: 8px 0;
}

.message-text.markdown-body :deep(pre code) {
  background: none;
  padding: 0;
  font-size: 12px;
  line-height: 1.4;
}

.message-text.markdown-body :deep(ul),
.message-text.markdown-body :deep(ol) {
  margin: 6px 0;
  padding-left: 20px;
}

.message-text.markdown-body :deep(li) {
  margin: 2px 0;
  font-size: 13px;
}

.message-text.markdown-body :deep(strong) {
  color: #93c5fd;
}

.message-text.markdown-body :deep(a) {
  color: #60a5fa;
  text-decoration: underline;
}

.message-text.markdown-body :deep(table) {
  width: 100%;
  border-collapse: collapse;
  margin: 8px 0;
  font-size: 12px;
}

.message-text.markdown-body :deep(th),
.message-text.markdown-body :deep(td) {
  border: 1px solid rgba(255, 255, 255, 0.15);
  padding: 4px 8px;
  text-align: left;
}

.message-text.markdown-body :deep(th) {
  background: rgba(59, 130, 246, 0.15);
  font-weight: 600;
}

.message-text.markdown-body :deep(blockquote) {
  border-left: 3px solid #3b82f6;
  padding-left: 10px;
  margin: 6px 0;
  color: #9ca3af;
}

.message-text.markdown-body :deep(hr) {
  border: none;
  border-top: 1px solid rgba(255, 255, 255, 0.1);
  margin: 10px 0;
}

.message.user .message-text {
  background: linear-gradient(135deg, #3b82f6, #2563eb);
  color: white;
}

.message-time {
  font-size: 10px;
  color: #6b7280;
  margin-top: 4px;
  padding: 0 4px;
}

.message.assistant .message-time {
  text-align: left;
}

.message.user .message-time {
  text-align: right;
}

.typing-indicator {
  display: flex;
  gap: 4px;
  padding: 12px;
  background: rgba(59, 130, 246, 0.1);
  border-radius: 12px;
  border: 1px solid rgba(59, 130, 246, 0.2);
}

.typing-indicator span {
  width: 6px;
  height: 6px;
  background: #3b82f6;
  border-radius: 50%;
  animation: typing 1.4s infinite;
}

.typing-indicator span:nth-child(2) {
  animation-delay: 0.2s;
}

.typing-indicator span:nth-child(3) {
  animation-delay: 0.4s;
}

@keyframes typing {
  0%, 60%, 100% {
    transform: translateY(0);
    opacity: 0.5;
  }
  30% {
    transform: translateY(-8px);
    opacity: 1;
  }
}

.input-area {
  display: flex;
  gap: 8px;
  padding: 12px;
  background: rgba(40, 40, 55, 0.9);
  border-top: 1px solid rgba(255, 255, 255, 0.1);
  align-items: flex-end;
}

.input-area textarea {
  flex: 1;
  padding: 10px 14px;
  background: rgba(255, 255, 255, 0.05);
  border: 1px solid rgba(255, 255, 255, 0.1);
  border-radius: 12px;
  color: #e5e7eb;
  font-size: 13px;
  line-height: 1.5;
  resize: none;
  outline: none;
  min-height: 72px;
  max-height: 180px;
  font-family: inherit;
  scrollbar-width: thin;
  scrollbar-color: rgba(255, 255, 255, 0.1) transparent;
}

.input-area textarea::-webkit-scrollbar {
  width: 6px;
}

.input-area textarea::-webkit-scrollbar-track {
  background: transparent;
}

.input-area textarea::-webkit-scrollbar-thumb {
  background: rgba(255, 255, 255, 0.1);
  border-radius: 3px;
}

.input-area textarea::-webkit-scrollbar-thumb:hover {
  background: rgba(255, 255, 255, 0.2);
}

.input-area textarea:focus {
  border-color: #3b82f6;
}

.input-area textarea:disabled {
  opacity: 0.5;
}

.send-btn {
  width: 36px;
  height: 36px;
  border-radius: 50%;
  background: linear-gradient(135deg, #3b82f6, #1d4ed8);
  border: none;
  color: white;
  cursor: pointer;
  display: flex;
  align-items: center;
  justify-content: center;
  transition: all 0.2s;
}

.send-btn:hover:not(:disabled) {
  transform: scale(1.1);
}

.send-btn:disabled {
  opacity: 0.5;
  cursor: not-allowed;
}

.empty-state {
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  padding: 40px 20px;
  color: #6b7280;
  text-align: center;
}

.empty-state i {
  font-size: 48px;
  margin-bottom: 12px;
  opacity: 0.3;
}

.empty-state p {
  font-size: 14px;
}

/* 滚动条样式 */
.message-list::-webkit-scrollbar {
  width: 4px;
}

.message-list::-webkit-scrollbar-track {
  background: transparent;
}

.message-list::-webkit-scrollbar-thumb {
  background: rgba(255, 255, 255, 0.1);
  border-radius: 2px;
}

.message-list::-webkit-scrollbar-thumb:hover {
  background: rgba(255, 255, 255, 0.2);
}
</style>
