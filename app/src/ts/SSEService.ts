// src/services/sseService.ts
import { ref, readonly, type Ref } from 'vue'
import { fetchEventSource } from '@microsoft/fetch-event-source'

export interface SSEMessage {
  type: string
  data: any
  timestamp: number
}

class SSEService {
  private abortController: AbortController | null = null
  private messageHandlers: Map<string, Function[]> = new Map()
  
  public messages: Ref<string[]> = ref([])
  public isConnected: Ref<boolean> = ref(false)
  public lastMessage: Ref<string | null> = ref(null)

  public readonlyMessages = readonly(this.messages)
  public readonlyIsConnected = readonly(this.isConnected)
  public readonlyLastMessage = readonly(this.lastMessage)

  /**
   * 使用 @microsoft/fetch-event-source 连接
   */
  async connect(token: string) {
    if (this.abortController) {
      this.disconnect()
    }

    this.abortController = new AbortController()
    const server = localStorage.getItem('remote')
    let url = 'https://' + server + '/v0/server/event'
    try {
      await fetchEventSource(url, {
        method: 'GET',
        headers: {
          'Authorization': `${token}`,
          'Content-Type': 'text/event-stream',
          'Cache-Control': 'no-cache'
        },
        signal: this.abortController.signal,
        
        onopen: async (response: any) => {
          console.log('SSE 连接已建立')
          this.isConnected.value = true
          console.info(response)
          if (response.ok) {
            return // 一切正常
          } else if (response.status >= 400 && response.status < 500) {
            throw new Error(`客户端错误: ${response.status}`)
          } else {
            throw new Error(`服务器错误: ${response.status}`)
          }
        },
        
        onmessage: (event: any) => {
          console.info('event', event)
          if (event.data) {
            this.handleEvent(event)
          }
        },
        
        onclose: () => {
          console.log('SSE 连接关闭')
          this.isConnected.value = false
        },
        
        onerror: (error) => {
          console.error('SSE 连接错误:', error)
          this.isConnected.value = false
          throw error // 重新抛出以停止重试
        }
      })
    } catch (error: any) {
      console.info('error:', error)
      if (error.name !== 'AbortError') {
        console.error('SSE 连接失败:', error)
      }
    }
  }

  /**
   * 处理事件
   */
  private handleEvent(event: any) {
    try {
      const eventType = event.event || 'message'
      const messageData = JSON.parse(event.data)
      const message: SSEMessage = {
        type: eventType,
        data: messageData,
        timestamp: Date.now()
      }

      this.messages.value.push(messageData)
      this.lastMessage.value = messageData

      this.triggerHandlers(eventType, message)
      // this.triggerHandlers('*', messageData)

      if (this.messages.value.length > 100) {
        this.messages.value = this.messages.value.slice(-50)
      }

    } catch (error) {
      console.error('解析 SSE 消息失败:', error, event.data)
    }
  }

  // 其他方法保持不变...
  on(messageType: string, handler: (message: string) => void) {
    if (!this.messageHandlers.has(messageType)) {
      this.messageHandlers.set(messageType, [])
    }
    this.messageHandlers.get(messageType)!.push(handler)
  }

  off(messageType: string, handler: (message: string) => void) {
    const handlers = this.messageHandlers.get(messageType)
    if (handlers) {
      const index = handlers.indexOf(handler)
      if (index > -1) {
        handlers.splice(index, 1)
      }
    }
  }

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

  disconnect() {
    if (this.abortController) {
      this.abortController.abort()
      this.abortController = null
    }
    this.isConnected.value = false
    this.messageHandlers.clear()
  }

  getMessagesByType(type: string): string[] {
    return this.messages.value.filter(msg => msg.type === type)
  }

  clearMessages() {
    this.messages.value = []
  }
}

export const sseService = new SSEService()
export default sseService