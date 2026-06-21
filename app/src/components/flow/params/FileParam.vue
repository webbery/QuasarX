<!-- src/components/flow/params/FileParam.vue -->
<template>
  <div class="file-input">
    <div class="file-path-display">
      <span class="path-text">{{ value || '未选择文件' }}</span>
      <button
        class="file-select-btn"
        @click="$emit('selectFile', paramKey)"
        @mousedown.stop
        @dragstart.stop
      >
        <i class="fas fa-file"></i>
        选择文件
      </button>
    </div>
  </div>
</template>

<script setup lang="ts">
defineProps<{
  paramKey: string
  paramConfig: any
  value: any
}>()

defineEmits<{
  selectFile: [paramKey: string]
}>()
</script>

<style scoped>
.file-input {
  width: 100% !important;
}

.file-path-display {
  display: flex !important;
  align-items: center !important;
  gap: 8px !important;
  background: rgba(0, 0, 0, 0.3) !important;
  border: 1px solid var(--border) !important;
  border-radius: 6px !important;
  padding: 4px 8px !important;
  min-height: 28px !important;
}

.file-path-display .path-text {
  flex: 1 !important;
  color: var(--text) !important;
  font-size: 0.75rem !important;
  overflow: hidden !important;
  text-overflow: ellipsis !important;
  white-space: nowrap !important;
  font-family: 'SF Mono', 'Consolas', monospace !important;
}

.file-select-btn {
  background: rgba(41, 98, 255, 0.15) !important;
  border: 1px solid rgba(41, 98, 255, 0.3) !important;
  border-radius: 4px !important;
  color: var(--text) !important;
  padding: 2px 8px !important;
  font-size: 0.7rem !important;
  cursor: pointer !important;
  transition: all 0.15s !important;
  display: flex !important;
  align-items: center !important;
  gap: 4px !important;
}

.file-select-btn:hover {
  background: rgba(41, 98, 255, 0.3) !important;
  border-color: rgba(41, 98, 255, 0.5) !important;
}
</style>
