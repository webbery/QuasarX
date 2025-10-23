import { ref, readonly, type Ref } from 'vue'

// SSE 消息类型定义
export interface SSEMessage {
  type: string
  data: any
  timestamp: number
}

class SSEService {
  private eventSource: EventSource | null = null
  private messageHandlers: Map<string, Function[]> = new Map()
  
  // 响应式数据，可以在 Vue 组件中使用
  public messages: Ref<SSEMessage[]> = ref([])
  public isConnected: Ref<boolean> = ref(false)
  public lastMessage: Ref<SSEMessage | null> = ref(null)

  // 只读版本，防止外部修改
  public readonlyMessages = readonly(this.messages)
  public readonlyIsConnected = readonly(this.isConnected)
  public readonlyLastMessage = readonly(this.lastMessage)

  /**
   * 连接到 SSE 服务器
   */
  connect(token: string) {
    if (this.eventSource) {
      this.disconnect()
    }

    try {
      // 创建 EventSource 连接，可以添加认证参数
      this.eventSource = new EventSource(`/server/event?token=${token}`)

      this.eventSource.onopen = () => {
        console.log('SSE 连接已建立')
        this.isConnected.value = true
      }

      this.eventSource.onmessage = (event) => {
        this.handleMessage(event)
      }

      this.eventSource.onerror = (error) => {
        console.error('SSE 连接错误:', error)
        this.isConnected.value = false
        // 可以在这里实现重连逻辑
      }

    } catch (error) {
      console.error('创建 SSE 连接失败:', error)
    }
  }

  /**
   * 处理接收到的消息
   */
  private handleMessage(event: MessageEvent) {
    try {
      const messageData = JSON.parse(event.data)
      const message: SSEMessage = {
        type: messageData.type || 'unknown',
        data: messageData.data,
        timestamp: Date.now()
      }

      // 更新响应式数据
      this.messages.value.push(message)
      this.lastMessage.value = message

      // 触发特定类型的处理器
      this.triggerHandlers(message.type, message)
      
      // 总是触发通用处理器
      this.triggerHandlers('*', message)

      // 保持消息列表不会无限增长（可选）
      if (this.messages.value.length > 100) {
        this.messages.value = this.messages.value.slice(-50)
      }

    } catch (error) {
      console.error('解析 SSE 消息失败:', error, event.data)
    }
  }

  /**
   * 注册消息处理器
   */
  on(messageType: string, handler: (message: SSEMessage) => void) {
    if (!this.messageHandlers.has(messageType)) {
      this.messageHandlers.set(messageType, [])
    }
    this.messageHandlers.get(messageType)!.push(handler)
  }

  /**
   * 取消注册消息处理器
   */
  off(messageType: string, handler: (message: SSEMessage) => void) {
    const handlers = this.messageHandlers.get(messageType)
    if (handlers) {
      const index = handlers.indexOf(handler)
      if (index > -1) {
        handlers.splice(index, 1)
      }
    }
  }

  /**
   * 触发处理器
   */
  private triggerHandlers(messageType: string, message: SSEMessage) {
    const handlers = this.messageHandlers.get(messageType)
    if (handlers) {
      handlers.forEach(handler => {
        try {
          handler(message)
        } catch (error) {
          console.error('SSE 消息处理器执行失败:', error)
        }
      })
    }
  }

  /**
   * 断开连接
   */
  disconnect() {
    if (this.eventSource) {
      this.eventSource.close()
      this.eventSource = null
    }
    this.isConnected.value = false
    this.messageHandlers.clear()
  }

  /**
   * 获取特定类型的消息
   */
  getMessagesByType(type: string): SSEMessage[] {
    return this.messages.value.filter(msg => msg.type === type)
  }

  /**
   * 清空消息历史
   */
  clearMessages() {
    this.messages.value = []
  }
}

// 创建单例实例
export const sseService = new SSEService()

// 默认导出
export default sseService