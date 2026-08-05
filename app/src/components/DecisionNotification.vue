<template>
  <div class="decision-notifications">
    <TransitionGroup name="toast">
      <div
        v-for="toast in toasts"
        :key="toast.id"
        class="toast"
        @click="onClickToast"
      >
        <div class="toast-icon">
          <i class="fas fa-bell"></i>
        </div>
        <div class="toast-content">
          <div class="toast-title">{{ toast.title }}</div>
          <div class="toast-body">{{ toast.body }}</div>
        </div>
        <div class="toast-close" @click.stop="removeToast(toast.id)">
          <i class="fas fa-times"></i>
        </div>
      </div>
    </TransitionGroup>
  </div>
</template>

<script setup lang="ts">
import { ref, onMounted, onUnmounted } from 'vue'
import sseService from '@/ts/SSEService'

interface Toast {
  id: number
  title: string
  body: string
}

const emit = defineEmits<{
  (e: 'navigate'): void
}>()

const toasts = ref<Toast[]>([])
let nextId = 0

const addToast = (title: string, body: string) => {
  const id = nextId++
  toasts.value.push({ id, title, body })
  setTimeout(() => removeToast(id), 5000)
}

const removeToast = (id: number) => {
  toasts.value = toasts.value.filter(t => t.id !== id)
}

const onClickToast = () => {
  toasts.value = []
  emit('navigate')
}

const onManualDecision = (messageData: any) => {
  try {
    const payload = JSON.parse(messageData.payload || messageData.data)
    const count = payload.decisions?.length || 0
    const strategy = payload.strategy || '未知策略'
    addToast(
      `${strategy} 产生交易决策`,
      `${count} 条决策待确认，点击查看详情`
    )
  } catch (e) {
    // ignore
  }
}

onMounted(() => {
  sseService.on('manual_decision', onManualDecision)
})

onUnmounted(() => {
  sseService.off('manual_decision', onManualDecision)
})
</script>

<style scoped>
.decision-notifications {
  position: fixed;
  bottom: 20px;
  right: 20px;
  z-index: 9999;
  display: flex;
  flex-direction: column-reverse;
  gap: 8px;
  max-width: 360px;
}

.toast {
  display: flex;
  align-items: flex-start;
  gap: 10px;
  padding: 12px 14px;
  background: rgba(30, 41, 59, 0.95);
  border: 1px solid rgba(74, 158, 255, 0.3);
  border-radius: 10px;
  cursor: pointer;
  backdrop-filter: blur(12px);
  box-shadow: 0 8px 24px rgba(0, 0, 0, 0.4);
  transition: all 0.2s;
}

.toast:hover {
  border-color: rgba(74, 158, 255, 0.5);
  transform: translateX(-4px);
}

.toast-icon {
  color: #60a5fa;
  font-size: 16px;
  margin-top: 2px;
  flex-shrink: 0;
}

.toast-content {
  flex: 1;
  min-width: 0;
}

.toast-title {
  font-size: 13px;
  font-weight: 600;
  color: #e2e8f0;
  margin-bottom: 2px;
}

.toast-body {
  font-size: 12px;
  color: #94a3b8;
}

.toast-close {
  color: #475569;
  font-size: 12px;
  cursor: pointer;
  padding: 2px;
  flex-shrink: 0;
  transition: color 0.2s;
}

.toast-close:hover {
  color: #94a3b8;
}

/* Transition animations */
.toast-enter-active {
  transition: all 0.3s ease-out;
}

.toast-leave-active {
  transition: all 0.2s ease-in;
}

.toast-enter-from {
  opacity: 0;
  transform: translateX(40px);
}

.toast-leave-to {
  opacity: 0;
  transform: translateX(40px);
}
</style>
