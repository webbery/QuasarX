<template>
  <Teleport to="body">
    <div v-if="visible" class="dialog-overlay" @click="handleCancel">
      <div class="dialog" @click.stop>
        <h3 class="dialog-title">{{ title }}</h3>
        <input
          ref="inputRef"
          v-model="inputValue"
          type="text"
          :placeholder="placeholder"
          class="dialog-input"
          @keydown.enter="handleConfirm"
        />
        <div class="dialog-actions">
          <button class="dialog-btn cancel" @click="handleCancel">取消</button>
          <button class="dialog-btn confirm" @click="handleConfirm">确定</button>
        </div>
      </div>
    </div>
  </Teleport>
</template>

<script setup lang="ts">
import { ref, nextTick } from 'vue'

const visible = ref(false)
const title = ref('')
const placeholder = ref('')
const inputValue = ref('')
let resolvePromise: ((result: { cancelled: boolean; value: string }) => void) | null = null

const inputRef = ref<HTMLInputElement>()

// 显示对话框，返回 Promise<{ cancelled: boolean; value: string }>
const show = (options: { title: string; placeholder?: string; defaultValue?: string }): Promise<{ cancelled: boolean; value: string }> => {
  title.value = options.title
  placeholder.value = options.placeholder || ''
  inputValue.value = options.defaultValue || ''
  visible.value = true

  // 自动聚焦
  nextTick(() => {
    inputRef.value?.focus()
  })

  return new Promise((resolve) => {
    resolvePromise = resolve
  })
}

const handleConfirm = () => {
  if (resolvePromise) {
    resolvePromise({ cancelled: false, value: inputValue.value })
  }
  visible.value = false
  resolvePromise = null
}

const handleCancel = () => {
  if (resolvePromise) {
    resolvePromise({ cancelled: true, value: '' })
  }
  visible.value = false
  resolvePromise = null
}

// 暴露方法给父组件
defineExpose({ show })
</script>

<style scoped>
/* 样式直接复制自 StrategyPanel 的对话框样式 */
.dialog-overlay {
  position: fixed;
  top: 0;
  left: 0;
  right: 0;
  bottom: 0;
  background-color: rgba(0, 0, 0, 0.6);
  display: flex;
  align-items: center;
  justify-content: center;
  z-index: 2000;
  backdrop-filter: blur(4px);
}

.dialog {
  background: var(--panel-bg);
  border-radius: 12px;
  padding: 24px;
  min-width: 320px;
  max-width: 400px;
  box-shadow: 0 12px 48px rgba(0, 0, 0, 0.4);
  border: 1px solid var(--border);
}

.dialog-title {
  margin: 0 0 20px 0;
  font-size: 1.2rem;
  font-weight: 600;
  color: var(--text);
  text-align: center;
}

.dialog-input {
  width: 100%;
  padding: 12px 16px;
  border: 1px solid var(--border);
  border-radius: 8px;
  background: var(--darker-bg);
  color: var(--text);
  font-size: 1rem;
  outline: none;
  transition: border-color 0.2s;
  box-sizing: border-box;
}

.dialog-input:focus {
  border-color: var(--primary);
}

.dialog-input::placeholder {
  color: var(--text-secondary);
}

.dialog-actions {
  display: flex;
  justify-content: flex-end;
  gap: 12px;
  margin-top: 20px;
}

.dialog-btn {
  padding: 8px 20px;
  border-radius: 8px;
  font-size: 0.95rem;
  font-weight: 500;
  cursor: pointer;
  transition: all 0.2s;
  border: none;
}

.dialog-btn.cancel {
  background: var(--darker-bg);
  color: var(--text);
  border: 1px solid var(--border);
}

.dialog-btn.cancel:hover {
  background: var(--border);
}

.dialog-btn.confirm {
  background: linear-gradient(90deg, #2563eb, #1d4ed8);
  color: white;
}

.dialog-btn.confirm:hover {
  background: linear-gradient(90deg, #1d4ed8, #1e40af);
  box-shadow: 0 4px 12px rgba(37, 99, 235, 0.3);
}
</style>