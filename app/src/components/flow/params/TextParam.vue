<!-- src/components/flow/params/TextParam.vue -->
<template>
  <div class="input-with-unit">
    <input
      type="text"
      :value="value"
      @input="onInput"
      @blur="validate"
      @keydown="$emit('keydown', $event)"
      @mousedown.stop
      @dragstart.stop
      class="param-input param-text"
      :class="{ 'input-error': showError }"
    />
    <span v-if="paramConfig.unit" class="param-unit">{{ paramConfig.unit }}</span>
    <div v-if="showError" class="input-error-msg">{{ errorMessage }}</div>
    <Handle
      v-if="isDataField"
      type="source"
      :position="Position.Right"
      :id="`field-${paramKey}`"
      class="field-output-handle"
    />
  </div>
</template>

<script setup lang="ts">
import { Handle, Position } from '@vue-flow/core'
import { computed, ref } from 'vue'

const props = defineProps<{
  paramKey: string
  paramConfig: any
  value: any
}>()

const emit = defineEmits<{
  update: [value: any]
  keydown: [event: KeyboardEvent]
}>()

const showError = ref(false)
const errorMessage = ref('')

const isDataField = computed(() => {
  return ['close', 'open', 'high', 'low', 'volume'].includes(props.paramKey)
})

const onInput = (event: Event) => {
  const value = (event.target as HTMLInputElement).value
  showError.value = false  // 输入时隐藏错误
  emit('update', value)
}

const validate = () => {
  if (props.paramConfig?.pattern) {
    const regex = new RegExp(props.paramConfig.pattern)
    if (!regex.test(props.value)) {
      showError.value = true
      errorMessage.value = props.paramConfig.errorMsg || '输入格式不正确'
    } else {
      showError.value = false
    }
  }
}
</script>

<style scoped>
.input-error {
  border-color: #ef4444 !important;
  background-color: #fef2f2 !important;
}

.input-error-msg {
  color: #ef4444 !important;
  font-size: 12px !important;
  margin-top: 4px !important;
  line-height: 1.4 !important;
}

/* 强制暗色主题样式 */
.param-input {
  background: rgba(0, 0, 0, 0.3) !important;
  border: 1px solid var(--border) !important;
  border-radius: 6px !important;
  color: var(--text) !important;
  padding: 5px 10px !important;
  font-size: 0.8rem !important;
  outline: none !important;
  transition: all 0.2s ease !important;
  width: 100% !important;
}

.param-input:focus {
  border-color: var(--primary) !important;
  box-shadow: 0 0 0 2px rgba(41, 98, 255, 0.2) !important;
  background: rgba(0, 0, 0, 0.4) !important;
}

.param-input:hover:not(:focus) {
  border-color: rgba(41, 98, 255, 0.5) !important;
}
</style>
