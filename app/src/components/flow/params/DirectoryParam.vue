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
