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
  width: 100%;
}

.file-path-display {
  display: flex;
  align-items: center;
  gap: 8px;
  background: rgba(0, 0, 0, 0.3);
  border: 1px solid var(--border, rgba(74, 158, 255, 0.3));
  border-radius: 6px;
  padding: 4px 8px;
  min-height: 28px;
}

.file-path-display .path-text {
  flex: 1;
  color: var(--text, #e2e8f0);
  font-size: 0.75rem;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
  font-family: 'SF Mono', 'Consolas', monospace;
}

.file-select-btn {
  background: rgba(41, 98, 255, 0.15);
  border: 1px solid rgba(41, 98, 255, 0.3);
  border-radius: 4px;
  color: var(--text, #e2e8f0);
  padding: 2px 8px;
  font-size: 0.7rem;
  cursor: pointer;
  transition: all 0.15s;
  display: flex;
  align-items: center;
  gap: 4px;
}

.file-select-btn:hover {
  background: rgba(41, 98, 255, 0.3);
  border-color: rgba(41, 98, 255, 0.5);
}
</style>
