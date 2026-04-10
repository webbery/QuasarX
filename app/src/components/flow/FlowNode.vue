<!-- src/components/flow/FlowNode.vue -->
<!-- 向后兼容层 - 重新导出 StrategyNode -->

<script setup lang="ts">
import StrategyNode from './StrategyNode.vue'

defineProps<{
    node: any
}>()

defineEmits(['update-node', 'node-click', 'node-context-menu'])
</script>

<template>
    <StrategyNode
        :node="node"
        @update-node="$emit('update-node', $event)"
        @node-click="$emit('node-click', $event)"
        @node-context-menu="$emit('node-context-menu', $event)"
    />
</template>
