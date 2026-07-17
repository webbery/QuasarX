<!-- src/components/flow/StrategyNodeHandles.vue -->
<!-- 节点连接点组件 -->

<template>
  <div class="node-connection-row">
    <!-- FunctionNode 命名输入槽位 -->
    <template v-if="nodeType === 'function' && functionSlots.length > 0">
      <div v-for="slot in functionSlots" :key="slot.slot" class="named-input-row">
        <Handle
          type="target"
          :position="Position.Left"
          :id="'input-' + slot.slot"
          class="connection-handle left-handle named-input-handle"
        />
        <span class="named-input-label">{{ slot.label }}</span>
        <span class="named-input-field">[{{ slot.field }}]</span>
      </div>
    </template>

    <!-- 普通节点：单一输入连接点 -->
    <Handle
      v-if="nodeType !== 'input' && nodeType !== 'function'"
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
import { functionInputSlots } from '@/lib/nodes/configs/function'

const props = defineProps<{
  nodeType: string
  params?: Record<string, any>
}>()

const shouldShowOutput = computed(() => {
  return !['output', 'input', 'execution'].includes(props.nodeType)
})

const functionSlots = computed(() => {
  if (props.nodeType !== 'function' || !props.params) return []
  const method = props.params.method?.value || 'MA'
  return functionInputSlots[method] || []
})
</script>

<style scoped>
.named-input-row {
  display: flex;
  align-items: center;
  gap: 6px;
  margin: 3px 0;
  position: relative;
}
.named-input-label {
  font-size: 11px;
  color: var(--text-secondary, #a0aec0);
  user-select: none;
}
.named-input-field {
  font-size: 10px;
  color: var(--text-tertiary, #718096);
  font-family: 'SF Mono', 'Consolas', monospace;
  user-select: none;
}
.named-input-handle {
  position: absolute;
  left: -12px;
}

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
