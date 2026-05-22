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

    <!-- HMM 特殊处理：多个独立输出 handles -->
    <template v-if="nodeType === 'hmm'">
      <div class="hmm-outputs">
        <Handle
          type="source"
          :position="Position.Right"
          id="hmm_state"
          class="connection-handle right-handle output-handle hmm-handle"
        />
        <span class="hmm-handle-label">状态</span>
      </div>
      <div class="hmm-outputs">
        <Handle
          type="source"
          :position="Position.Right"
          id="hmm_probs"
          class="connection-handle right-handle output-handle hmm-handle"
        />
        <span class="hmm-handle-label">概率</span>
      </div>
      <div class="hmm-outputs">
        <Handle
          type="source"
          :position="Position.Right"
          id="hmm_transition"
          class="connection-handle right-handle output-handle hmm-handle"
        />
        <span class="hmm-handle-label">转移</span>
      </div>
      <div class="hmm-outputs">
        <Handle
          type="source"
          :position="Position.Right"
          id="hmm_duration"
          class="connection-handle right-handle output-handle hmm-handle"
        />
        <span class="hmm-handle-label">持续</span>
      </div>
    </template>

    <!-- 其他节点：单一输出连接点 -->
    <Handle
      v-if="shouldShowOutput && nodeType !== 'hmm'"
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

<style scoped>
.hmm-outputs {
  display: flex;
  align-items: center;
  gap: 4px;
  margin: 2px 0;
}
.hmm-handle-label {
  font-size: 10px;
  color: var(--text-secondary, #a0aec0);
  user-select: none;
}
</style>
