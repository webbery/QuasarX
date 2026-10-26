<template>
  <div class="ml-step-flow">
    <div
      v-for="node in nodes"
      :key="node.key"
      class="flow-node"
      :class="{ done: node.done, failed: node.failed, locked: node.locked, active: node.active }"
      :title="node.title"
      @click="$emit('select', node.key)"
    >
      <span class="node-index">{{ node.index }}</span>
      <span class="node-label">{{ node.label }}</span>
      <span class="node-summary">{{ node.summary }}</span>
      <span class="node-status">{{ node.statusText }}</span>
    </div>
  </div>
</template>

<script setup lang="ts">
import type { FlowNode } from '@/types/ml'

defineProps<{
  nodes: FlowNode[]
}>()

defineEmits<{
  select: [key: string]
}>()
</script>

<style scoped>
.ml-step-flow {
  display: flex;
  gap: 8px;
  padding: 8px 12px;
  align-items: center;
}

.flow-node {
  display: flex;
  flex-direction: column;
  gap: 2px;
  padding: 6px 10px;
  background: rgba(15, 25, 41, 0.6);
  border: 1px solid rgba(74, 85, 104, 0.3);
  border-radius: 4px;
  cursor: pointer;
  transition: all 0.2s;
  min-width: 120px;
}

.flow-node:hover {
  background: rgba(15, 25, 41, 0.8);
  border-color: rgba(91, 143, 249, 0.4);
}

.flow-node.active {
  background: rgba(91, 143, 249, 0.15);
  border-color: rgba(91, 143, 249, 0.5);
}

.flow-node.done {
  border-color: rgba(38, 166, 91, 0.4);
}

.flow-node.failed {
  border-color: rgba(234, 57, 67, 0.4);
}

.flow-node.locked {
  opacity: 0.6;
}

.node-index {
  font-size: 10px;
  color: #5b8ff9;
  font-family: 'SF Mono', 'Consolas', monospace;
}

.node-label {
  font-size: 12px;
  color: #e2e8f0;
  font-weight: 600;
}

.node-summary {
  font-size: 10px;
  color: #94a3b8;
  font-family: 'SF Mono', 'Consolas', monospace;
}

.node-status {
  font-size: 9px;
  color: #64748b;
  margin-top: 2px;
}
</style>
