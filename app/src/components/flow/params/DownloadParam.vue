<!-- src/components/flow/params/DownloadParam.vue -->
<template>
  <div class="download-section">
    <button
      class="download-btn"
      @click="$emit('download', paramKey)"
      :disabled="!canDownload"
      @mousedown.stop
      @dragstart.stop
    >
      <i class="fas fa-download"></i>
      下载文件
    </button>
    <span v-if="downloadStatus" class="download-status">{{ downloadStatus }}</span>
  </div>
</template>

<script setup lang="ts">
defineProps<{
  paramKey: string
  paramConfig: any
  value: any
  canDownload: boolean
  downloadStatus: string
}>()

defineEmits<{
  download: [paramKey: string]
}>()
</script>

<style scoped>
.download-section {
  display: flex;
  align-items: center;
  gap: 8px;
}

.download-btn {
  background: rgba(41, 98, 255, 0.15);
  border: 1px solid rgba(41, 98, 255, 0.3);
  border-radius: 4px;
  color: var(--text, #e2e8f0);
  padding: 4px 12px;
  font-size: 0.75rem;
  cursor: pointer;
  transition: all 0.15s;
  display: flex;
  align-items: center;
  gap: 4px;
}

.download-btn:hover:not(:disabled) {
  background: rgba(41, 98, 255, 0.3);
  border-color: rgba(41, 98, 255, 0.5);
}

.download-btn:disabled {
  opacity: 0.4;
  cursor: not-allowed;
}

.download-status {
  color: var(--text-secondary, #94a3b8);
  font-size: 0.7rem;
}
</style>
