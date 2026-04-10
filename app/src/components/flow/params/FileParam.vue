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
