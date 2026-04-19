<template>
    <g>
        <defs>
            <marker
                id="arrow-default"
                markerWidth="10"
                markerHeight="7"
                refX="9"
                refY="3.5"
                orient="auto"
                markerUnits="userSpaceOnUse"
            >
                <polygon
                    points="0 0, 10 3.5, 0 7"
                    fill="var(--primary)"
                />
            </marker>
            <marker
                id="arrow-selected"
                markerWidth="10"
                markerHeight="7"
                refX="9"
                refY="3.5"
                orient="auto"
                markerUnits="userSpaceOnUse"
            >
                <polygon
                    points="0 0, 10 3.5, 0 7"
                    fill="#ff6b35"
                />
            </marker>
        </defs>
        <path
            :d="connectionLinePath"
            fill="none"
            :stroke="props.selected ? '#ff6b35' : (connectionLineStyle?.stroke || 'var(--primary)')"
            :stroke-width="props.selected ? 4 : (connectionLineStyle?.strokeWidth || 2)"
            :marker-end="props.selected ? 'url(#arrow-selected)' : 'url(#arrow-default)'"
            :class="{ 'connection-line': true, selected: props.selected }"
            @click.stop="onClick"
        />
        <circle
            v-if="targetPosition"
            :cx="targetPosition.x"
            :cy="targetPosition.y"
            r="4"
            :fill="props.selected ? '#ff6b35' : (connectionLineStyle?.stroke || 'var(--primary)')"
        />
    </g>
</template>

<script setup>
import { computed, watch, onMounted, onUnmounted } from 'vue'
import { getBezierPath } from '@vue-flow/core'

// 定义组件事件
const emit = defineEmits(['edge-click', 'edge-select'])

// 定义组件属性
const props = defineProps({
    id: {
        type: String,
        default: ''
    },
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
    targetPosition: String,
    selected: {
        type: Boolean,
        default: false
    },
    // 可能由 Vue Flow 传入的原始 edge 对象
    edge: {
        type: Object,
        default: null
    },
    source: String,
    target: String,
    sourceHandle: String,
    targetHandle: String
})

console.log('[FlowConnectLine] === MOUNTED ===, props keys:', Object.keys(props), 'selected:', props.selected, 'id:', props.id)

// 调试：打印 props 变化
watch(() => ({ ...props }), (newProps) => {
    console.log('[FlowConnectLine] Props updated:', {
        id: newProps.id,
        selected: newProps.selected,
        sourceX: newProps.sourceX,
        sourceY: newProps.sourceY,
        targetX: newProps.targetX,
        targetY: newProps.targetY,
        hasEdge: !!newProps.edge,
        style: newProps.connectionLineStyle,
        allKeys: Object.keys(newProps)
    })
}, { deep: true, immediate: true })

// 计算连接线路径
const connectionLinePath = computed(() => {
    if (props.sourceX !== undefined && props.sourceY !== undefined &&
        props.targetX !== undefined && props.targetY !== undefined) {
        const path = getBezierPath({
            sourceX: props.sourceX,
            sourceY: props.sourceY,
            sourcePosition: props.sourcePosition || 'right',
            targetX: props.targetX,
            targetY: props.targetY,
            targetPosition: props.targetPosition || 'left',
        })
        console.log('[FlowConnectLine] Path computed OK, id:', props.id)
        return path
    }
    console.warn('[FlowConnectLine] Missing coordinates, returning empty path. sourceX:', props.sourceX, 'targetX:', props.targetX)
    return ''
})

// 计算目标位置
const targetPosition = computed(() => ({
  x: props.targetX,
  y: props.targetY
}))

// 事件处理
const onClick = (event) => {
  console.log('[FlowConnectLine] >>> PATH CLICKED! id:', props.id, 'selected:', props.selected, 'event:', event)
  event.stopPropagation()
  emit('edge-click', {
    id: props.id,
    edge: props.edge,
    event,
    selected: props.selected
  })
}

</script>
<style scoped>
.connection-line {
  cursor: pointer;
  transition: stroke 0.2s, stroke-width 0.2s;
}

.connection-line.selected {
  stroke: #ff6b35 !important;
  stroke-width: 4 !important;
  filter: drop-shadow(0 0 6px rgba(255, 107, 53, 0.6));
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
