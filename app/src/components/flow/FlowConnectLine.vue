<template>
    <g>
        <path
            :d="connectionLinePath"
            fill="none"
            :stroke="connectionLineStyle?.stroke || 'var(--primary)'"
            :stroke-width="connectionLineStyle?.strokeWidth || 2"
            @click="onClick"
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
    sourcePosition: String,
    targetPosition: String
})

// 计算连接线路径 - 使用更安全的方式
const connectionLinePath = computed(() => {
    // 优先使用直接传递的坐标
    if (props.sourceX !== undefined && props.sourceY !== undefined && 
        props.targetX !== undefined && props.targetY !== undefined) {
        return getBezierPath({
            sourceX: props.sourceX,
            sourceY: props.sourceY,
            sourcePosition: props.sourcePosition || 'right', // 默认位置
            targetX: props.targetX,
            targetY: props.targetY,
            targetPosition: props.targetPosition || 'left',   // 默认位置
        })
    }
    console.info('connection line path')
    // 如果都没有有效数据，返回空路径
    return ''
})

// 计算目标位置
const targetPosition = computed(() => ({
  x: props.targetX,
  y: props.targetY
}))

// 事件处理
const onClick = (event) => {
  emit('edge-click', { edge: props, event })
}

</script>
<style scoped>
.connection-line {
  stroke-dasharray: 5;
  animation: dashdraw 0.5s linear infinite;
}

@keyframes dashdraw {
  from {
    stroke-dashoffset: 10;
  }
}

.connection-target {
  animation: pulse 1.5s ease-in-out infinite;
}

@keyframes pulse {
  0%, 100% {
    opacity: 1;
    transform: scale(1);
  }
  50% {
    opacity: 0.7;
    transform: scale(1.2);
  }
}
</style>