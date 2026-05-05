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

defineEmits<{
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
  props.update(value)
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
  background-color: #fef2f2;
}

.input-error-msg {
  color: #ef4444;
  font-size: 12px;
  margin-top: 4px;
  line-height: 1.4;
}
</style>
