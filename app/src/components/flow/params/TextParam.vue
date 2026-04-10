<!-- src/components/flow/params/TextParam.vue -->
<template>
  <div class="input-with-unit">
    <input
      type="text"
      :value="value"
      @input="$emit('update', ($event.target as HTMLInputElement).value)"
      @keydown="$emit('keydown', $event)"
      @mousedown.stop
      @dragstart.stop
      class="param-input param-text"
    />
    <span v-if="paramConfig.unit" class="param-unit">{{ paramConfig.unit }}</span>
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
import { computed } from 'vue'

const props = defineProps<{
  paramKey: string
  paramConfig: any
  value: any
}>()

defineEmits<{
  update: [value: any]
  keydown: [event: KeyboardEvent]
}>()

const isDataField = computed(() => {
  return ['close', 'open', 'high', 'low', 'volume'].includes(props.paramKey)
})
</script>
