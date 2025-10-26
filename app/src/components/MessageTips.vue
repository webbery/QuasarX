<template>
  <!-- 过渡组支持多个消息同时显示 -->
  <transition-group tag="div" name="slide-fade" class="message-wrapper">
    <div 
      v-for="message in messageQueue" 
      :key="message.id" 
      :class="['alert-message', `alert-${message.type}`]"
    >
      <div class="message-content">
        <!-- 显示图标和内容 -->
        <i :class="getIconClass(message.type)"></i>
        <span class="message-text">{{ message.content }}</span>
      </div>
      <!-- 可手动关闭 -->
      <button v-if="message.closable" class="close-btn" @click="closeMessage(message)">
        x
      </button>
    </div>
  </transition-group>
</template>

<script setup lang="ts">
import { ref, computed, onUnmounted } from 'vue'

// 定义消息类型
type MessageType = 'success' | 'info' | 'warning' | 'error'

// 消息接口
interface Message {
  id: string | number
  content: string
  type: MessageType
  duration?: number
  closable?: boolean
}

// 消息队列
const messages = ref<Message[]>([])

// 计算属性，限制最大显示数量
const messageQueue = computed(() => 
  messages.value.slice(-5) // 最多同时显示5条
)

// 添加消息
const addMessage = (message: Message) => {
  messages.value.push(message)
  
  // 设置自动关闭
  if (message.duration !== 0) {
    setTimeout(() => {
      removeMessage(message.id)
    }, message.duration || 3000)
  }
}

// 移除消息
const removeMessage = (id: string | number) => {
  const index = messages.value.findIndex(msg => msg.id === id)
  if (index > -1) {
    messages.value.splice(index, 1)
  }
}

// 关闭消息
const closeMessage = (message: Message) => {
  removeMessage(message.id)
}

// 获取图标类名
const getIconClass = (type: MessageType) => {
  const iconMap = {
    success: 'icon-success',
    info: 'icon-info',
    warning: 'icon-warning',
    error: 'icon-error'
  }
  return iconMap[type]
}

// 暴露方法给外部使用
defineExpose({
  addMessage,
  removeMessage
})
</script>

<style scoped>
.message-wrapper {
  position: fixed;
  bottom: 30px;
  right: 30px;
  z-index: 9999;
}

.alert-message {
  position: relative;
  display: flex;
  align-items: flex-start;
  min-width: 450px;
  max-width: 600px;
  padding: 25px 70px 25px 25px;
  border-radius: 12px;
  box-shadow: 0 8px 32px rgba(0, 0, 0, 0.15);
  transition: all 0.4s cubic-bezier(0.175, 0.885, 0.32, 1.275);
  background: var(--panel-bg);
  border-left: 6px solid;
  overflow: hidden;
  font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
}

.alert-success {
  border-color: #67c23a;
  color: #67c23a;
}

.alert-info {
  border-color: #909399;
  color: #909399;
}

.alert-warning {
  border-color: #e6a23c;
  color: #e6a23c;
}

.alert-error {
  border-color: #f56c6c;
  color: #f56c6c;
}

.message-content {
  display: flex;
  align-items: center;
  flex-grow: 1;
}

.message-text {
  margin-left: 8px;
}

.close-btn {
  background: none;
  border: none;
  font-size: 18px;
  cursor: pointer;
  color: inherit;
  opacity: 0.7;
}

.close-btn:hover {
  opacity: 1;
}

/* 过渡动画 */
.slide-fade-enter-active,
.slide-fade-leave-active {
  transition: all 1s ease;
}

.slide-fade-enter-from {
  opacity: 0;
  transform: translateX(30px);
}

.slide-fade-leave-to {
  opacity: 0;
  transform: translateX(30px);
}
</style>