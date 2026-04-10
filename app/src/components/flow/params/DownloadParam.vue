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
