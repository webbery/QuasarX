import { defineStore } from 'pinia'
import { ref } from 'vue'

export interface ChatMessage {
  id: string
  role: 'user' | 'assistant' | 'system'
  content: string
  timestamp: number
}

export interface Live2DSettings {
  enabled: boolean         // 总开关
  model: string           // 当前模型名
  volume: number          // 音量 0-1
  autoGreet: boolean      // 自动问候
  showQuickActions: boolean  // 显示快捷按钮
}

export interface Live2DState {
  visible: boolean           // 是否显示助手
  position: 'left' | 'right' // 吸附位置
  y: number                  // Y 轴位置
  messages: ChatMessage[]    // 聊天记录
  currentMotion: string      // 当前播放的动作
  isDragging: boolean        // 是否正在拖拽
  settings: Live2DSettings
  isLoading: boolean         // 是否正在加载 AI 回复
}

const DEFAULT_SETTINGS: Live2DSettings = {
  enabled: true,
  model: 'default',
  volume: 0.5,
  autoGreet: true,
  showQuickActions: true,
}

export const useLive2DStore = defineStore('live2d', () => {
  const visible = ref(false)
  const position = ref<'left' | 'right'>('right')
  const y = ref(100)
  const messages = ref<ChatMessage[]>([])
  const currentMotion = ref('idle')
  const isDragging = ref(false)
  const isLoading = ref(false)
  const settings = ref<Live2DSettings>({ ...DEFAULT_SETTINGS })

  // 从 localStorage 加载配置
  function loadFromStorage() {
    try {
      const stored = localStorage.getItem('quasarx_live2d')
      if (stored) {
        const parsed = JSON.parse(stored)
        if (parsed.position) position.value = parsed.position
        if (parsed.y !== undefined) y.value = parsed.y
        if (parsed.settings) {
          settings.value = { ...DEFAULT_SETTINGS, ...parsed.settings }
        }
      }
    } catch (e) {
      console.warn('[Live2D] 加载本地配置失败', e)
    }
  }

  // 保存配置到 localStorage
  function saveToStorage() {
    try {
      const data = {
        position: position.value,
        y: y.value,
        settings: settings.value,
      }
      localStorage.setItem('quasarx_live2d', JSON.stringify(data))
    } catch (e) {
      console.warn('[Live2D] 保存本地配置失败', e)
    }
  }

  // 切换显示/隐藏
  function toggle() {
    visible.value = !visible.value
    if (visible.value && messages.value.length === 0 && settings.value.autoGreet) {
      addGreeting()
    }
    saveToStorage()
  }

  // 设置可见性
  function setVisible(val: boolean) {
    visible.value = val
    if (val && messages.value.length === 0 && settings.value.autoGreet) {
      addGreeting()
    }
    saveToStorage()
  }

  // 添加消息
  function addMessage(msg: Omit<ChatMessage, 'id' | 'timestamp'>) {
    const message: ChatMessage = {
      ...msg,
      id: Date.now().toString() + Math.random().toString(36).slice(2, 8),
      timestamp: Date.now(),
    }
    messages.value.push(message)
    // 限制消息历史长度
    if (messages.value.length > 50) {
      messages.value = messages.value.slice(-50)
    }
  }

  // 清空消息
  function clearMessages() {
    messages.value = []
  }

  // 设置当前动作
  function setMotion(motion: string) {
    currentMotion.value = motion
    // 自动恢复待机动作
    if (motion !== 'idle') {
      setTimeout(() => {
        if (currentMotion.value === motion) {
          currentMotion.value = 'idle'
        }
      }, 3000)
    }
  }

  // 设置位置
  function setPosition(pos: 'left' | 'right', yPos?: number) {
    position.value = pos
    if (yPos !== undefined) {
      y.value = yPos
    }
    saveToStorage()
  }

  // 更新设置
  function updateSettings(newSettings: Partial<Live2DSettings>) {
    settings.value = { ...settings.value, ...newSettings }
    saveToStorage()
  }

  // 添加问候语
  function addGreeting() {
    const hour = new Date().getHours()
    let greeting = '你好！'
    if (hour < 12) {
      greeting = '早上好！'
    } else if (hour < 18) {
      greeting = '下午好！'
    } else {
      greeting = '晚上好！'
    }
    greeting += '我是 QuasarX AI 助手，有什么可以帮您的吗？'
    
    addMessage({
      role: 'assistant',
      content: greeting,
    })
  }

  // 加载配置
  loadFromStorage()

  return {
    visible,
    position,
    y,
    messages,
    currentMotion,
    isDragging,
    isLoading,
    settings,
    toggle,
    setVisible,
    addMessage,
    clearMessages,
    setMotion,
    setPosition,
    updateSettings,
    addGreeting,
  }
})
