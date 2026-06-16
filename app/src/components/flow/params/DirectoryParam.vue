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
  width: 100%;
}

.directory-path-display {
  display: flex;
  align-items: center;
  gap: 8px;
  background: rgba(0, 0, 0, 0.3);
  border: 1px solid var(--border, rgba(74, 158, 255, 0.3));
  border-radius: 6px;
  padding: 4px 8px;
  min-height: 28px;
}

.directory-path-display .path-text {
  flex: 1;
  color: var(--text, #e2e8f0);
  font-size: 0.75rem;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
  font-family: 'SF Mono', 'Consolas', monospace;
}

.directory-select-btn {
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

.directory-select-btn:hover {
  background: rgba(41, 98, 255, 0.3);
  border-color: rgba(41, 98, 255, 0.5);
}
</style>
