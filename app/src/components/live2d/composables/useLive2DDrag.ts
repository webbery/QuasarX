/**
 * Live2D 拖拽吸附逻辑
 */

import { ref, onMounted, onUnmounted } from 'vue'

export interface DragState {
  x: number                 // X 位置
  y: number                 // Y 位置
  edge: 'left' | 'right'   // 吸附边缘
  isDragging: boolean      // 是否正在拖拽
}

const ASSISTANT_WIDTH = 360  // 助手容器宽度
const SNAP_THRESHOLD = 50    // 吸附阈值（px）
const MIN_Y = 60             // 最小 Y（避免被 header 遮挡）
const MAX_Y_OFFSET = 100     // 距底部最小距离

export function useLive2DDrag() {
  const x = ref(window.innerWidth - ASSISTANT_WIDTH - 20)  // 初始在右侧
  const y = ref(100)
  const edge = ref<'left' | 'right'>('right')
  const isDragging = ref(false)

  let dragStartX = 0
  let dragStartY = 0
  let startStateX = 0
  let startStateY = 0

  // 窗口尺寸变化时重新计算
  function handleResize() {
    if (!isDragging.value) {
      snapToEdge()
    }
  }

  // 吸附到边缘
  function snapToEdge() {
    const centerX = x.value + ASSISTANT_WIDTH / 2
    
    if (centerX < SNAP_THRESHOLD) {
      edge.value = 'left'
      x.value = 0
    } else if (centerX > window.innerWidth - SNAP_THRESHOLD) {
      edge.value = 'right'
      x.value = window.innerWidth - ASSISTANT_WIDTH
    }
  }

  // 开始拖拽
  function onDragStart(event: MouseEvent | TouchEvent) {
    isDragging.value = true
    
    if (event instanceof MouseEvent) {
      dragStartX = event.clientX
      dragStartY = event.clientY
    } else if (event instanceof TouchEvent) {
      dragStartX = event.touches[0].clientX
      dragStartY = event.touches[0].clientY
    }
    
    startStateX = x.value
    startStateY = y.value

    // 拖拽时禁用过渡动画
    document.addEventListener('mousemove', onDragMove)
    document.addEventListener('mouseup', onDragEnd)
    document.addEventListener('touchmove', onDragMove)
    document.addEventListener('touchend', onDragEnd)
  }

  // 拖拽移动
  function onDragMove(event: MouseEvent | TouchEvent) {
    if (!isDragging.value) return

    let currentX: number, currentY: number
    if (event instanceof MouseEvent) {
      currentX = event.clientX
      currentY = event.clientY
    } else {
      currentX = event.touches[0].clientX
      currentY = event.touches[0].clientY
    }

    const deltaX = currentX - dragStartX
    const deltaY = currentY - dragStartY

    // 计算新位置
    let newX = startStateX + deltaX
    let newY = startStateY + deltaY

    // 边界限制
    const maxY = window.innerHeight - MAX_Y_OFFSET - 400 // 400 是容器高度估算
    newY = Math.max(MIN_Y, Math.min(newY, maxY))
    newX = Math.max(-ASSISTANT_WIDTH / 2, Math.min(newX, window.innerWidth - ASSISTANT_WIDTH / 2))

    x.value = newX
    y.value = newY
  }

  // 结束拖拽
  function onDragEnd() {
    isDragging.value = false
    
    // 吸附到边缘
    snapToEdge()

    // 移除事件监听
    document.removeEventListener('mousemove', onDragMove)
    document.removeEventListener('mouseup', onDragEnd)
    document.removeEventListener('touchmove', onDragMove)
    document.removeEventListener('touchend', onDragEnd)
  }

  onMounted(() => {
    window.addEventListener('resize', handleResize)
    // 初始化位置
    snapToEdge()
  })

  onUnmounted(() => {
    window.removeEventListener('resize', handleResize)
    document.removeEventListener('mousemove', onDragMove)
    document.removeEventListener('mouseup', onDragEnd)
    document.removeEventListener('touchmove', onDragMove)
    document.removeEventListener('touchend', onDragEnd)
  })

  return {
    x,
    y,
    edge,
    isDragging,
    onDragStart,
  }
}
