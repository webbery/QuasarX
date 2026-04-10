<!-- src/components/flow/params/TextareaParam.vue -->
<template>
  <div class="textarea-wrapper">
    <textarea
      :value="value"
      @input="$emit('update', ($event.target as HTMLTextAreaElement).value)"
      @keydown="$emit('keydown', $event)"
      @mousedown.stop
      @dragstart.stop
      class="param-textarea"
      :placeholder="paramConfig.placeholder || ''"
      rows="3"
    ></textarea>
  </div>
</template>

<script setup lang="ts">
defineProps<{
  paramKey: string
  paramConfig: any
  value: any
}>()

defineEmits<{
  update: [value: any]
  keydown: [event: KeyboardEvent]
}>()
</script>
