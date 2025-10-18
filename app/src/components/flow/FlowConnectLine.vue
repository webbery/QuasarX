<template>
    <g>
        <path
            :d="connectionLinePath"
            fill="none"
            :stroke="connectionLineStyle?.stroke || 'var(--primary)'"
            :stroke-width="connectionLineStyle?.strokeWidth || 2"
        />
        <circle
            :cx="connectionPosition.targetX"
            :cy="connectionPosition.targetY"
            r="4"
            :fill="connectionLineStyle?.stroke || 'var(--primary)'"
        />
    </g>
</template>

<script setup>
import { BaseEdge, getBezierPath } from '@vue-flow/core'

// 定义组件属性
const props = defineProps({
    connectionLineStyle: {
        type: Object,
        default: () => ({})
    },
    connectionPosition: {
        type: Object,
        required: true
    }
})

// 计算连接线路径
const connectionLinePath = getBezierPath({
    sourceX: props.connectionPosition.sourceX,
    sourceY: props.connectionPosition.sourceY,
    targetX: props.connectionPosition.targetX,
    targetY: props.connectionPosition.targetY,
})
</script>