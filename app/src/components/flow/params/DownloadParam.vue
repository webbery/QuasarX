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
  display: flex !important;
  align-items: center !important;
  gap: 8px !important;
}

.download-btn {
  background: rgba(41, 98, 255, 0.15) !important;
  border: 1px solid rgba(41, 98, 255, 0.3) !important;
  border-radius: 4px !important;
  color: var(--text) !important;
  padding: 4px 12px !important;
  font-size: 0.75rem !important;
  cursor: pointer !important;
  transition: all 0.15s !important;
  display: flex !important;
  align-items: center !important;
  gap: 4px !important;
}

.download-btn:hover:not(:disabled) {
  background: rgba(41, 98, 255, 0.3) !important;
  border-color: rgba(41, 98, 255, 0.5) !important;
}

.download-btn:disabled {
  opacity: 0.4 !important;
  cursor: not-allowed !important;
}

.download-status {
  color: var(--text-secondary) !important;
  font-size: 0.7rem !important;
}
</style>
