<template>
  <Teleport to="body">
    <div
      v-if="live2dStore.visible"
      class="live2d-assistant"
      :class="{
        'dragging': dragIsDragging,
        'edge-left': dragEdge === 'left',
        'edge-right': dragEdge === 'right',
      }"
      :style="containerStyle"
    >
      <!-- 关闭按钮 -->
      <button class="close-btn" @click="handleClose" title="关闭">
        <i class="fas fa-times"></i>
      </button>

      <!-- Live2D 模型 -->
      <div
        class="model-area"
        @mousedown="dragState.onDragStart"
        @touchstart.passive="dragState.onDragStart"
      >
        <Live2DModel
          ref="modelRef"
          :model-name="live2dStore.settings.model"
          :current-motion="live2dStore.currentMotion"
          @click="handleModelClick"
        />
      </div>

      <!-- 聊天面板 -->
      <div class="chat-area">
        <Live2DChatPanel ref="chatPanelRef" />
      </div>
    </div>
  </Teleport>
</template>

<script setup lang="ts">
import { computed, ref, watch, onMounted } from 'vue'
import { useLive2DStore } from '@/stores/live2dStore'
import { useLive2DDrag } from '@/components/live2d/composables/useLive2DDrag'
import Live2DModel from './Live2DModel.vue'
import Live2DChatPanel from './Live2DChatPanel.vue'
import { findGestureMapping } from '@/lib/live2d/gestureMapper'

const live2dStore = useLive2DStore()
const modelRef = ref<InstanceType<typeof Live2DModel>>()
const chatPanelRef = ref<InstanceType<typeof Live2DChatPanel>>()

// 拖拽逻辑
const { x: dragX, y: dragY, edge: dragEdge, isDragging: dragIsDragging, onDragStart: handleDragStart } = useLive2DDrag()

// 容器样式
const containerStyle = computed(() => ({
  left: `${dragX.value}px`,
  top: `${dragY.value}px`,
}))

// 暴露拖拽状态给模板使用
const dragState = {
  onDragStart: handleDragStart,
  get value() {
    return {
      x: dragX.value,
      y: dragY.value,
      edge: dragEdge.value,
      isDragging: dragIsDragging.value,
    }
  }
}

// 关闭助手
function handleClose() {
  live2dStore.setVisible(false)
}

// 模型点击
function handleModelClick() {
  // 可以触发一些交互，比如挥手
  if (modelRef.value) {
    modelRef.value.playMotion('wave')
  }
}

// 监听视图变化，触发动作
// 注意：这里需要从 App.vue 传入当前视图
// 暂时使用 localStorage 或其他方式获取

onMounted(() => {
  // 首次显示时播放问候动作
  if (live2dStore.visible) {
    setTimeout(() => {
      modelRef.value?.playMotion('wave')
    }, 1000)
  }
})
</script>

<style scoped>
.live2d-assistant {
  position: fixed;
  z-index: 9999;
  width: 360px;
  max-height: calc(100vh - 120px);
  background: rgba(20, 20, 30, 0.95);
  border-radius: 12px;
  box-shadow: 0 8px 32px rgba(0, 0, 0, 0.4);
  backdrop-filter: blur(10px);
  border: 1px solid rgba(255, 255, 255, 0.1);
  overflow: hidden;
  transition: left 0.3s cubic-bezier(0.4, 0, 0.2, 1),
              top 0.1s linear,
              opacity 0.2s ease;
}

.live2d-assistant.dragging {
  transition: none;
  box-shadow: 0 12px 48px rgba(0, 0, 0, 0.5);
}

.close-btn {
  position: absolute;
  top: 8px;
  right: 8px;
  width: 28px;
  height: 28px;
  border-radius: 50%;
  background: rgba(239, 68, 68, 0.8);
  border: none;
  color: white;
  cursor: pointer;
  display: flex;
  align-items: center;
  justify-content: center;
  z-index: 10;
  transition: all 0.2s;
}

.close-btn:hover {
  background: rgba(239, 68, 68, 1);
  transform: scale(1.1);
}

.model-area {
  height: 400px;
  display: flex;
  align-items: center;
  justify-content: center;
  cursor: grab;
  background: linear-gradient(180deg, 
    rgba(59, 130, 246, 0.05) 0%, 
    transparent 100%
  );
  border-bottom: 1px solid rgba(255, 255, 255, 0.05);
}

.model-area:active {
  cursor: grabbing;
}

.chat-area {
  max-height: calc(100vh - 560px);
}

/* 边缘吸附视觉反馈 */
.edge-left::before,
.edge-right::before {
  content: '';
  position: absolute;
  top: 50%;
  width: 3px;
  height: 40px;
  background: linear-gradient(180deg, transparent, #3b82f6, transparent);
  transform: translateY(-50%);
  border-radius: 2px;
}

.edge-left::before {
  left: 0;
}

.edge-right::before {
  right: 0;
}
</style>
