<!-- Label 参数组件：显示文本标签，右侧可带 Handle -->
<template>
  <div class="label-param-row">
    <span class="label-param-text">{{ paramConfig.label || paramKey }}</span>
    <Handle
      v-if="handleId"
      type="source"
      :position="Position.Right"
      :id="handleId"
      class="connection-handle right-handle output-handle label-handle"
    />
  </div>
</template>

<script setup lang="ts">
import { Handle, Position } from '@vue-flow/core'

defineProps<{
  paramKey: string
  paramConfig: { label: string; description?: string }
  handleId?: string
}>()
</script>

<style scoped>
.label-param-row {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 4px 14px;
  position: relative;
}
.label-param-text {
  font-size: 11px;
  color: var(--text-secondary, #a0aec0);
  font-weight: 500;
  user-select: none;
}
</style>
