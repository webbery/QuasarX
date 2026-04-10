<!-- src/components/flow/StrategyNodeHandles.vue -->
<!-- 节点连接点组件 -->

<template>
  <div class="node-connection-row">
    <!-- 左侧输入连接点 -->
    <Handle
      v-if="nodeType !== 'input'"
      type="target"
      :position="Position.Left"
      id="input"
      class="connection-handle left-handle input-handle"
    />

    <!-- 右侧输出连接点 -->
    <Handle
      v-if="shouldShowOutput"
      type="source"
      :position="Position.Right"
      id="output"
      class="connection-handle right-handle output-handle"
    />
  </div>
</template>

<script setup lang="ts">
import { Handle, Position } from '@vue-flow/core'
import { computed } from 'vue'

const props = defineProps<{
  nodeType: string
}>()

const shouldShowOutput = computed(() => {
  return !['output', 'input', 'execution'].includes(props.nodeType)
})
</script>
