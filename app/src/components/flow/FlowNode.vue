<!-- src/components/flow/FlowNode.vue -->
<!-- 向后兼容层 - 重新导出 StrategyNode -->

<script setup lang="ts">
import StrategyNode from './StrategyNode.vue'

defineProps<{
    node: any
}>()

const emit = defineEmits(['update-node', 'node-click', 'node-context-menu'])

// 转发 StrategyNode 的多参数事件（$event 只捕获第一个参数，需用展开语法）
const forwardUpdate = (nodeId: string, paramKey: string, newValue: any) => {
    emit('update-node', nodeId, paramKey, newValue)
}
</script>

<template>
    <StrategyNode
        :node="node"
        @update-node="forwardUpdate"
        @node-click="$emit('node-click', $event)"
        @node-context-menu="$emit('node-context-menu', $event)"
    />
</template>
