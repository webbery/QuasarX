<!-- src/components/flow/params/NumberParam.vue -->
<template>
  <div class="input-with-unit">
    <input
      type="number"
      :value="value"
      @input="$emit('update', ($event.target as HTMLInputElement).value)"
      :min="paramConfig.min"
      :max="paramConfig.max"
      :step="paramConfig.step || 1"
      @mousedown.stop
      @dragstart.stop
      class="param-input param-number"
    />
    <span v-if="paramConfig.unit" class="param-unit">{{ paramConfig.unit }}</span>
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
}>()
</script>

<style scoped>
/* 强制暗色主题样式 - number 输入框 */
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
