<template>
  <div class="live2d-model-container" ref="containerRef">
    <canvas ref="canvasRef" class="live2d-canvas"></canvas>
    <div v-if="isLoading" class="loading-overlay">
      <i class="fas fa-spinner fa-spin"></i>
      <span>加载中...</span>
    </div>
    <div v-if="error" class="error-overlay">
      <i class="fas fa-exclamation-triangle"></i>
      <span>{{ error }}</span>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, onMounted, onUnmounted, watch } from 'vue'
import * as PIXI from 'pixi.js'
// @ts-ignore - pixi-live2d-display 类型声明兼容性问题
import { Live2DModel } from 'pixi-live2d-display'
import { getModelPath } from '@/lib/live2d/modelLoader'

const props = withDefaults(defineProps<{
  modelName?: string
  currentMotion?: string
}>(), {
  modelName: 'default',
  currentMotion: 'idle',
})

const emit = defineEmits<{
  (e: 'click'): void
  (e: 'dragStart', event: MouseEvent | TouchEvent): void
  (e: 'loaded'): void
  (e: 'error', error: string): void
}>()

const containerRef = ref<HTMLDivElement>()
const canvasRef = ref<HTMLCanvasElement>()
const isLoading = ref(false)
const error = ref('')

let app: PIXI.Application | null = null
let live2dModel: Live2DModel | null = null

// 初始化 PixiJS 应用
async function initPixi() {
  if (!canvasRef.value) return

  try {
    isLoading.value = true
    error.value = ''

    app = new PIXI.Application({
      view: canvasRef.value,
      width: 300,
      height: 400,
      backgroundAlpha: 0,  // PixiJS 7 使用 backgroundAlpha
      antialias: true,
      resolution: window.devicePixelRatio || 1,
    })

    // 加载 Live2D 模型
    const modelPath = getModelPath(props.modelName)
    console.log('[Live2D] 加载模型:', modelPath)

    live2dModel = await Live2DModel.from(modelPath, {
      autoInteract: false,
    })

    // 设置模型大小和位置
    const scale = 0.8
    live2dModel.scale.set(scale)
    live2dModel.anchor.set(0.5, 0.5)
    live2dModel.x = app.screen.width / 2
    live2dModel.y = app.screen.height / 2 + 50

    app.stage.addChild(live2dModel as any)

    // 点击事件
    // @ts-ignore - PixiJS 7 兼容性
    live2dModel.eventMode = 'static'
    live2dModel.on('pointertap', () => {
      emit('click')
    })

    emit('loaded')
  } catch (err: any) {
    error.value = `模型加载失败: ${err.message}`
    emit('error', err.message)
    console.error('[Live2D] 模型加载失败:', err)
  } finally {
    isLoading.value = false
  }
}

// 播放动作
function playMotion(motionName: string) {
  if (!live2dModel || !motionName) return

  try {
    // @ts-ignore - Live2DModel 类型声明不完整
    live2dModel.motion(motionName)
  } catch (err) {
    console.warn('[Live2D] 播放动作失败:', err)
  }
}

// 监听动作变化
watch(() => props.currentMotion, (newMotion) => {
  if (newMotion && live2dModel) {
    playMotion(newMotion)
  }
})

// 说话动画
function startSpeaking() {
  if (live2dModel) {
    playMotion('speak')
  }
}

function stopSpeaking() {
  if (live2dModel) {
    playMotion('idle')
  }
}

// 暴露方法给父组件
defineExpose({
  startSpeaking,
  stopSpeaking,
  playMotion,
})

onMounted(() => {
  initPixi()
})

onUnmounted(() => {
  // 清理资源
  if (app) {
    app.destroy(true)
    app = null
  }
  // @ts-ignore - Live2DModel 类型声明不完整
  if (live2dModel) {
    live2dModel.destroy()
    live2dModel = null
  }
})
</script>

<style scoped>
.live2d-model-container {
  position: relative;
  width: 300px;
  height: 400px;
  cursor: grab;
}

.live2d-model-container:active {
  cursor: grabbing;
}

.live2d-canvas {
  width: 100%;
  height: 100%;
  display: block;
}

.loading-overlay,
.error-overlay {
  position: absolute;
  top: 0;
  left: 0;
  right: 0;
  bottom: 0;
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  background: rgba(0, 0, 0, 0.3);
  color: white;
  gap: 8px;
  border-radius: 8px;
}

.loading-overlay i {
  font-size: 24px;
}

.error-overlay i {
  font-size: 24px;
  color: #ef4444;
}

.error-overlay span {
  font-size: 12px;
  text-align: center;
  padding: 0 10px;
}
</style>
