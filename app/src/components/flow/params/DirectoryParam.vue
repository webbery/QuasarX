<!-- src/components/flow/params/DirectoryParam.vue -->
<template>
  <div class="directory-input">
    <div class="directory-path-display">
      <span class="path-text">{{ value || '未选择路径' }}</span>
      <button
        class="directory-select-btn"
        @click="$emit('selectDirectory', paramKey)"
        @mousedown.stop
        @dragstart.stop
      >
        <i class="fas fa-folder-open"></i>
        选择路径
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
  selectDirectory: [paramKey: string]
}>()
</script>

<style scoped>
.directory-input {
  width: 100% !important;
}

.directory-path-display {
  display: flex !important;
  align-items: center !important;
  gap: 8px !important;
  background: rgba(0, 0, 0, 0.3) !important;
  border: 1px solid var(--border) !important;
  border-radius: 6px !important;
  padding: 4px 8px !important;
  min-height: 28px !important;
}

.directory-path-display .path-text {
  flex: 1 !important;
  color: var(--text) !important;
  font-size: 0.75rem !important;
  overflow: hidden !important;
  text-overflow: ellipsis !important;
  white-space: nowrap !important;
  font-family: 'SF Mono', 'Consolas', monospace !important;
}

.directory-select-btn {
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

.directory-select-btn:hover {
  background: rgba(41, 98, 255, 0.3) !important;
  border-color: rgba(41, 98, 255, 0.5) !important;
}
</style>
