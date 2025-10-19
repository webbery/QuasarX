<template>
    <g>
        <path
            :d="connectionLinePath"
            fill="none"
            :stroke="connectionLineStyle?.stroke || 'var(--primary)'"
            :stroke-width="connectionLineStyle?.strokeWidth || 2"
        />
        <circle
            v-if="targetPosition"
            :cx="targetPosition.x"
            :cy="targetPosition.y"
            r="4"
            :fill="connectionLineStyle?.stroke || 'var(--primary)'"
        />
    </g>
</template>

<script setup>
import { computed, inject } from 'vue'
import { getBezierPath } from '@vue-flow/core'

// 定义组件属性
const props = defineProps({
    connectionLineStyle: {
        type: Object,
        default: () => ({})
    },
    // 使用 Vue Flow 标准属性名
    sourceX: Number,
    sourceY: Number,
    targetX: Number,
    targetY: Number,
    sourcePosition: Object,
    targetPosition: Object
})

// 计算连接线路径 - 使用更安全的方式
const connectionLinePath = computed(() => {
    // 优先使用直接传递的坐标
    if (props.sourceX !== undefined && props.sourceY !== undefined && 
        props.targetX !== undefined && props.targetY !== undefined) {
        return getBezierPath({
            sourceX: props.sourceX,
            sourceY: props.sourceY,
            targetX: props.targetX,
            targetY: props.targetY,
        })
    }
    
    // 备用方案：使用位置对象
    if (props.sourcePosition && props.targetPosition) {
        return getBezierPath({
            sourceX: props.sourcePosition.x,
            sourceY: props.sourcePosition.y,
            targetX: props.targetPosition.x,
            targetY: props.targetPosition.y,
        })
    }
    
    // 如果都没有有效数据，返回空路径
    console.warn('FlowConnectLine: 缺少有效的坐标数据')
    return ''
})
</script>