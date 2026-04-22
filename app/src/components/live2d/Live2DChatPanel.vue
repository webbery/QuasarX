<template>
  <div class="live2d-chat-panel">
    <!-- 快捷按钮 -->
    <div v-if="live2dStore.settings.showQuickActions" class="quick-actions">
      <button @click="handleQuickAction('market')" title="行情分析">
        <i class="fas fa-chart-line"></i>
        <span>行情</span>
      </button>
      <button @click="handleQuickAction('strategy')" title="策略建议">
        <i class="fas fa-cogs"></i>
        <span>策略</span>
      </button>
      <button @click="handleQuickAction('risk')" title="风险预警">
        <i class="fas fa-shield-alt"></i>
        <span>风险</span>
      </button>
      <button @click="handleQuickAction('position')" title="持仓诊断">
        <i class="fas fa-briefcase"></i>
        <span>持仓</span>
      </button>
    </div>

    <!-- 消息列表 -->
    <div class="message-list" ref="messageListRef">
      <div
        v-for="msg in live2dStore.messages"
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
      <div v-if="live2dStore.isLoading" class="message assistant">
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
      <div v-if="live2dStore.messages.length === 0 && !live2dStore.isLoading" class="empty-state">
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
        :disabled="live2dStore.isLoading"
        rows="1"
      ></textarea>
      <button
        @click="sendMessage"
        :disabled="!inputText.trim() || live2dStore.isLoading"
        class="send-btn"
      >
        <i class="fas fa-paper-plane"></i>
      </button>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, watch, nextTick } from 'vue'
import { useLive2DStore } from '@/stores/live2dStore'
import { askLive2D, askMarketAnalysis, askStrategyAdvice, askRiskWarning, askPositionDiagnosis } from '@/lib/live2d/Live2DApi'
import { useLive2DContext } from '@/components/live2d/composables/useLive2DContext'

const live2dStore = useLive2DStore()
const inputText = ref('')
const messageListRef = ref<HTMLDivElement>()

// 获取当前视图（从父组件传入或从路由获取）
const currentViewRef = ref('account') // 默认值，实际应由父组件传入
const { context } = useLive2DContext(currentViewRef)

// 自动滚动到底部
watch(() => live2dStore.messages, async () => {
  await nextTick()
  if (messageListRef.value) {
    messageListRef.value.scrollTop = messageListRef.value.scrollHeight
  }
}, { deep: true })

// 发送消息
async function sendMessage() {
  const text = inputText.value.trim()
  if (!text || live2dStore.isLoading) return

  // 添加用户消息
  live2dStore.addMessage({
    role: 'user',
    content: text,
  })

  inputText.value = ''
  live2dStore.isLoading = true
  live2dStore.setMotion('think')

  try {
    // 调用 AI
    const response = await askLive2D(text, {
      context: context.value,
    })

    live2dStore.isLoading = false
    live2dStore.setMotion('speak')

    // 添加 AI 回复
    live2dStore.addMessage({
      role: 'assistant',
      content: response,
    })

    // 3秒后恢复待机
    setTimeout(() => {
      live2dStore.setMotion('idle')
    }, 3000)
  } catch (error) {
    live2dStore.isLoading = false
    live2dStore.setMotion('alert')
    console.error('[Live2D] 发送消息失败:', error)
  }
}

// 快捷操作
async function handleQuickAction(type: string) {
  live2dStore.setMotion('wave')
  
  let question = ''
  let response = ''

  switch (type) {
    case 'market':
      response = await askMarketAnalysis()
      break
    case 'strategy':
      response = await askStrategyAdvice()
      break
    case 'risk':
      response = await askRiskWarning()
      break
    case 'position':
      response = await askPositionDiagnosis()
      break
  }

  live2dStore.setMotion('speak')
  live2dStore.addMessage({
    role: 'assistant',
    content: response,
  })

  setTimeout(() => {
    live2dStore.setMotion('idle')
  }, 3000)
}

// 格式化时间
function formatTime(timestamp: number): string {
  const date = new Date(timestamp)
  const hours = date.getHours().toString().padStart(2, '0')
  const minutes = date.getMinutes().toString().padStart(2, '0')
  return `${hours}:${minutes}`
}

// 暴露方法供父组件更新当前视图
function updateCurrentView(view: string) {
  currentViewRef.value = view
}

defineExpose({ updateCurrentView })
</script>

<style scoped>
.live2d-chat-panel {
  display: flex;
  flex-direction: column;
  height: 100%;
  background: rgba(30, 30, 40, 0.95);
  border-radius: 8px;
  overflow: hidden;
}

.quick-actions {
  display: flex;
  gap: 4px;
  padding: 8px;
  background: rgba(40, 40, 55, 0.9);
  border-bottom: 1px solid rgba(255, 255, 255, 0.1);
}

.quick-actions button {
  flex: 1;
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 2px;
  padding: 6px 4px;
  background: rgba(59, 130, 246, 0.1);
  border: 1px solid rgba(59, 130, 246, 0.3);
  border-radius: 6px;
  color: #60a5fa;
  cursor: pointer;
  font-size: 11px;
  transition: all 0.2s;
}

.quick-actions button:hover {
  background: rgba(59, 130, 246, 0.2);
  transform: translateY(-1px);
}

.quick-actions button i {
  font-size: 14px;
}

.message-list {
  flex: 1;
  overflow-y: auto;
  padding: 12px;
  display: flex;
  flex-direction: column;
  gap: 12px;
  min-height: 200px;
  max-height: 350px;
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
