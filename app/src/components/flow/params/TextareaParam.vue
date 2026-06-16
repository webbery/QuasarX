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

<style scoped>
.textarea-wrapper {
  width: 100%;
}

.param-textarea {
  width: 100%;
  background: rgba(0, 0, 0, 0.3);
  border: 1px solid var(--border, rgba(74, 158, 255, 0.3));
  border-radius: 6px;
  color: var(--text, #e2e8f0);
  padding: 5px 10px;
  font-size: 0.8rem;
  outline: none;
  transition: all 0.2s ease;
  resize: vertical;
  font-family: inherit;
}

.param-textarea:focus {
  border-color: var(--primary, #2962ff);
  box-shadow: 0 0 0 2px rgba(41, 98, 255, 0.2);
  background: rgba(0, 0, 0, 0.4);
}

.param-textarea:hover:not(:focus) {
  border-color: rgba(41, 98, 255, 0.5);
}

.param-textarea::placeholder {
  color: var(--text-secondary, #64748b);
}
</style>
