<template>
  <div class="flow-container">
    <VueFlow
      :nodes="nodes"
      :edges="edges"
      @pane-ready="handlePaneReady"
      @drop="handleDrop"
      @dragover="handleDragOver"
      @node-context-menu="handleNodeContextMenu"
      @edge-click="handleEdgeClick"
      @selection-drag-start="handleSelectionDragStart"
      @selection-drag="handleSelectionDrag"
      @selection-drag-stop="handleSelectionDragStop"
      @selection-context-menu="handleSelectionContextMenu"
      @pane-click="handlePaneClick"
      @connect="handleConnect"
      @edges-delete="handleEdgesDelete"
      :is-valid-connection="isValidConnection"
    >
      <template #node-custom="nodeProps">
        <FlowNode 
          :node="nodeProps"
          @update-node="handleUpdateNode"
          @node-click="handleNodeClick"
          @node-context-menu="handleNodeContextMenu"
        />
      </template>

      <!-- 自定义连接线样式 -->
      <template #connection-line="connectionProps">
        <FlowConnectLine v-bind="connectionProps" @edge-click="handleConnectionClick" />
      </template>

      <!-- 自定义边样式：使用 edge-default（Vue Flow 默认 edge type） -->
      <template #edge-default="edgeProps">
        <FlowConnectLine
          :id="edgeProps.id"
          :source-x="edgeProps.sourceX"
          :source-y="edgeProps.sourceY"
          :target-x="edgeProps.targetX"
          :target-y="edgeProps.targetY"
          :source-position="edgeProps.sourcePosition"
          :target-position="edgeProps.targetPosition"
          :connection-line-style="edgeProps.style"
          :selected="edgeProps.selected"
          :edge="edgeProps"
          @edge-click="handleEdgeClickFromCustom"
        />
      </template>
    </VueFlow>
  </div>
</template>

<script setup>
import { VueFlow } from '@vue-flow/core'
import { watch } from 'vue'
import FlowNode from './flow/FlowNode.vue'
import FlowConnectLine from './flow/FlowConnectLine.vue'

const props = defineProps({
  nodes: {
    type: Array,
    required: true
  },
  edges: {
    type: Array,
    required: true
  },
  isValidConnection: {
    type: Function,
    default: () => true
  }
})

const emit = defineEmits([
  'pane-ready',
  'drop',
  'dragover',
  'node-context-menu',
  'edge-click',
  'selection-drag-start',
  'selection-drag',
  'selection-drag-stop',
  'selection-context-menu',
  'pane-click',
  'connect',
  'edges-delete',
  'node-click',
  'update-node'
])

const handlePaneReady = (vueFlowInstance) => {
  emit('pane-ready', vueFlowInstance)
}

const handleDrop = (event) => {
  emit('drop', event)
}

const handleDragOver = (event) => {
  emit('dragover', event)
}

const handleNodeContextMenu = (event) => {
  emit('node-context-menu', event)
}

const handleEdgeClick = (event) => {
  console.log('[FlowCanvas] VueFlow native edge-click:', event)
  emit('edge-click', event)
}

const handleEdgeClickFromCustom = (event) => {
  console.log('[FlowCanvas] Custom edge-click from FlowConnectLine:', event)
  emit('edge-click', event)
}

const handleConnectionClick = (event) => {
  console.log('[FlowCanvas] Connection-line click:', event)
  emit('edge-click', event)
}

const handleSelectionDragStart = (event) => {
  emit('selection-drag-start', event)
}

const handleSelectionDrag = (event) => {
  emit('selection-drag', event)
}

const handleSelectionDragStop = (event) => {
  emit('selection-drag-stop', event)
}

const handleSelectionContextMenu = (event) => {
  emit('selection-context-menu', event)
}

const handlePaneClick = (event) => {
  emit('pane-click', event)
}

const handleConnect = (connection) => {
  emit('connect', connection)
}

const handleEdgesDelete = (deletedEdges) => {
  emit('edges-delete', deletedEdges)
}

const handleNodeClick = (event) => {
  emit('node-click', event)
}

const handleUpdateNode = (nodeId, paramKey, newValue) => {
  emit('update-node', nodeId, paramKey, newValue)
}

// 调试：监听 edges 变化
watch(() => props.edges, (newEdges) => {
  console.log('[FlowCanvas] Edges changed, count:', newEdges?.length, 'edges:', newEdges?.map(e => ({ id: e.id, selected: e.selected })))
}, { deep: true, immediate: true })

// 暴露 fitView 方法供父组件调用
const fitView = (options) => {
  // 需要通过 VueFlow 实例调用
  // 这里由父组件通过 ref 访问
}

defineExpose({
  fitView
})
</script>

<style scoped>
.flow-container {
  flex: 1;
  height: 100%;
  position: relative;
}

/* 自定义节点样式 */
.vue-flow__node-custom {
  padding: 10px;
  border-radius: 8px;
  border: 2px solid var(--primary);
  background: var(--panel-bg);
  box-shadow: 0 2px 5px rgba(0, 0, 0, 0.2);
  min-width: 150px;
  font-size: 0.9rem;
  color: var(--text);
}

.vue-flow__node-custom.selected {
  border-color: rgba(205, 87, 41, 0.892) !important;
  box-shadow: 0 0 5px 1px rgba(205, 109, 0, 0.8);
}

/* 多选状态样式 */
.vue-flow__node-custom.multi-selected {
  border-width: 3px;
  border-color: rgba(205, 87, 41, 0.892) !important;
  box-shadow: 0 0 5px 1px rgba(208, 109, 0, 0.8);
  transform: translateY(-1px);
  z-index: 1000;
}

.node-content {
  padding: 5px 0;
}

.node-param {
  font-size: 0.8rem;
  margin: 3px 0;
  color: var(--text-secondary);
}

.vue-flow__edge {
  cursor: pointer;
}

.vue-flow__edge-path {
  stroke: var(--border);
  stroke-width: 2;
}

.vue-flow__edge.selected .vue-flow__edge-path {
  stroke: #ff6b35;
  stroke-width: 4;
  filter: drop-shadow(0 0 6px rgba(255, 107, 53, 0.6));
}

.vue-flow__edge.animated path {
  stroke-dasharray: 5;
  animation: dashdraw 0.5s linear infinite;
}

@keyframes dashdraw {
  from { stroke-dashoffset: 10; }
}

.vue-flow__connectionline {
  z-index: 1000;
}
</style>
