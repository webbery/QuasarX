<template>
  <div class="info-panel">
    <!-- 消息列表 -->
    <div class="info-panel-content" ref="contentRef">
      <div
        v-for="(msg, index) in messages"
        :key="index"
        class="info-message"
        :class="`type-${msg.type}`"
      >
        <!-- 进度条消息 -->
        <template v-if="msg.type === 'progress'">
          <span class="info-timestamp">{{ formatTime(msg.timestamp) }}</span>
          <span class="info-text">
            <span class="progress-strategy">{{ msg.strategy }}</span>
            <span class="inline-progress">
              <span class="inline-bar" :style="{ width: msg.progress * 100 + '%' }"></span>
            </span>
            {{ Math.round(msg.progress * 100) }}%
          </span>
        </template>

        <!-- 普通消息 -->
        <template v-else>
          <span class="info-timestamp">{{ formatTime(msg.timestamp) }}</span>
          <span class="info-text">{{ msg.text }}</span>
        </template>
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, watch, nextTick } from 'vue'

// 使 Math 在模板中可用
const Math = globalThis.Math

interface ProgressMessage {
  type: 'progress'
  backtestId: string
  strategy: string
  progress: number
  message: string
  timestamp: Date
}

interface TextMessage {
  type: 'info' | 'warning' | 'error' | 'success'
  text: string
  timestamp: Date
}

type Message = ProgressMessage | TextMessage

const props = defineProps<{
  messages: Message[]
}>()

const contentRef = ref<HTMLElement | null>(null)

// 格式化时间显示
const formatTime = (date: Date): string => {
  return date.toLocaleTimeString('zh-CN', {
    hour12: false,
    hour: '2-digit',
    minute: '2-digit',
    second: '2-digit'
  })
}

// 监听消息变化自动滚动到底部
watch(() => props.messages, () => {
  nextTick(() => {
    if (contentRef.value) {
      contentRef.value.scrollTop = contentRef.value.scrollHeight
    }
  })
}, { deep: true })
</script>

<style scoped>
/* 信息面板样式 */
.info-panel {
  position: absolute;
  bottom: 20px;
  left: 20px;
  width: 380px;
  max-height: 300px;
  background: rgba(30, 33, 45, 0.95);
  backdrop-filter: blur(10px);
  border: 1px solid rgba(255, 255, 255, 0.1);
  border-radius: 8px;
  z-index: 1000;
  display: flex;
  flex-direction: column;
  box-shadow: 0 4px 12px rgba(0, 0, 0, 0.3);
  overflow: hidden;
}

.info-panel-content {
  flex: 1;
  overflow-y: auto;
  padding: 8px;
  font-size: 12px;
  scrollbar-width: thin;
  scrollbar-color: var(--primary) transparent;
  background: rgba(30, 33, 45, 0.5);
}

.info-panel-content::-webkit-scrollbar {
  width: 6px;
}

.info-panel-content::-webkit-scrollbar-track {
  background: transparent;
}

.info-panel-content::-webkit-scrollbar-thumb {
  background-color: var(--primary);
  border-radius: 3px;
}

.info-message {
  margin-bottom: 8px;
  border-radius: 4px;
  background: transparent;
  animation: fadeIn 0.3s ease;
}

@keyframes fadeIn {
  from { opacity: 0; transform: translateY(-5px); }
  to { opacity: 1; transform: translateY(0); }
}

/* 进度条消息样式 */
.progress-message-item {
  padding: 10px;
  background: rgba(41, 98, 255, 0.1);
  border: 1px solid rgba(41, 98, 255, 0.3);
  border-radius: 6px;
}

.progress-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 6px;
}

.progress-strategy {
  font-size: 13px;
  font-weight: 600;
  color: var(--primary);
}

.progress-percent {
  font-size: 14px;
  font-weight: 700;
  color: var(--secondary);
}

.progress-message {
  font-size: 12px;
  color: var(--text-secondary);
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
  display: block;
}

.inline-progress {
  display: inline-block;
  width: 60px;
  height: 10px;
  background: rgba(255, 255, 255, 0.1);
  border-radius: 5px;
  overflow: hidden;
  vertical-align: middle;
  margin: 0 6px;
  position: relative;
}

.inline-bar {
  display: inline-block;
  height: 100%;
  background: linear-gradient(90deg, var(--primary), var(--secondary));
  border-radius: 5px;
  transition: width 0.3s ease;
  box-shadow: 0 0 8px rgba(41, 98, 255, 0.5);
}

/* 普通消息样式 */
.info-message.type-info,
.info-message.type-success,
.info-message.type-warning,
.info-message.type-error {
  display: flex;
}

.info-timestamp {
  color: var(--text-secondary);
  font-size: 10px;
  white-space: nowrap;
  min-width: 60px;
  padding-top: 1px;
  display: inline-block;
}

.info-text {
  color: var(--text);
  flex: 1;
  word-break: break-word;
}

.info-message.type-info .info-text {
  color: var(--text);
}

.info-message.type-success .info-text {
  color: var(--secondary);
}

.info-message.type-warning .info-text {
  color: #ff9800;
}

.info-message.type-error .info-text {
  color: #f44336;
}
</style>
