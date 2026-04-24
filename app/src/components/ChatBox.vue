<template>
  <Teleport to="body">
    <div
      v-if="chatStore.visible"
      class="chat-box"
      :style="containerStyle"
    >
      <!-- 头部 -->
      <div class="chat-header">
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
          <div class="message-content">
            <div class="message-text">{{ msg.content }}</div>
            <div class="message-time">{{ formatTime(msg.timestamp) }}</div>
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

        <!-- 空状态 -->
        <div v-if="chatStore.messages.length === 0 && !chatStore.isLoading" class="empty-state">
          <i class="fas fa-comments"></i>
          <p>有什么可以帮您的？</p>
        </div>
      </div>

      <!-- 输入框 -->
      <div class="input-area">
        <textarea
          v-model="inputText"
          @keydown.enter.exact.prevent="sendMessage"
          placeholder="输入问题..."
          :disabled="chatStore.isLoading"
          rows="1"
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
import { useChatStore } from '@/stores/chatStore'
import { askAI } from '@/lib/ChatApi'

const chatStore = useChatStore()
const inputText = ref('')
const messageListRef = ref<HTMLDivElement>()

// 拖拽相关
const isDragging = ref(false)
const dragStart = ref({ x: 0, y: 0 })
const containerPosition = ref({ right: 20, bottom: 80 })

const containerStyle = computed(() => ({
  right: `${containerPosition.value.right}px`,
  bottom: `${containerPosition.value.bottom}px`,
}))

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
    // 调用 AI，注入行情上下文
    const response = await askAI(text, {
      context: chatStore.marketContext,
    })

    chatStore.isLoading = false

    // 添加 AI 回复
    chatStore.addMessage({
      role: 'assistant',
      content: response,
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
</script>

<style scoped>
.chat-box {
  position: fixed;
  z-index: 9999;
  width: 380px;
  height: 600px;
  max-height: calc(100vh - 120px);
  background: #1e1e2e;
  border-radius: 12px;
  box-shadow: 0 8px 32px rgba(0, 0, 0, 0.4);
  border: 1px solid rgba(255, 255, 255, 0.1);
  display: flex;
  flex-direction: column;
  overflow: hidden;
}

.chat-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 12px 16px;
  background: linear-gradient(135deg, #3b82f6, #1d4ed8);
  color: white;
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

.message-content {
  max-width: 75%;
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
  padding: 10px;
  background: rgba(40, 40, 55, 0.9);
  border-top: 1px solid rgba(255, 255, 255, 0.1);
}

.input-area textarea {
  flex: 1;
  padding: 8px 12px;
  background: rgba(255, 255, 255, 0.05);
  border: 1px solid rgba(255, 255, 255, 0.1);
  border-radius: 20px;
  color: #e5e7eb;
  font-size: 13px;
  resize: none;
  outline: none;
  max-height: 80px;
  font-family: inherit;
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
