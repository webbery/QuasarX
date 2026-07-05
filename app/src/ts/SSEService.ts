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
  
  public messages: Ref<SSEMessage[]> = ref([])
  public isConnected: Ref<boolean> = ref(false)
  public lastMessage: Ref<SSEMessage | null> = ref(null)

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
          this.isConnected.value = true
          console.log('SSE 连接已建立')
          console.info(response)
          if (response.ok) {
            return // 一切正常
          } else if (response.status >= 400 && response.status < 500) {
            console.log('SSE 客户端错误')
            throw new Error(`客户端错误: ${response.status}`)
          } else {
            console.log('SSE 服务器错误')
            throw new Error(`服务器错误: ${response.status}`)
          }
        },
        
        onmessage: (event: any) => {
          console.log(`[SSE] onmessage 原始事件:`, event)
          console.log(`[SSE] onmessage: event.event=${event.event ? `"${event.event}"` : 'undefined'}, event.data 存在=${!!event.data}`)
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
      console.log(`[SSE] handleEvent: event.event=${event.event ? `"${event.event}"` : 'undefined'}, 使用 eventType="${eventType}"`)
      console.log(`[SSE] handleEvent: event.data 前 100 字符=${event.data?.substring(0, 100)}`)
      
      const messageData = JSON.parse(event.data)
      const message: SSEMessage = {
        type: eventType,
        data: messageData,
        timestamp: Date.now()
      }

      this.messages.value.push(messageData)
      this.lastMessage.value = messageData

      console.log(`[SSE] handleEvent: 准备触发 triggerHandlers("${eventType}")`)
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
    console.log(`[SSE] on() 注册 handler: ${messageType}, 当前 handler 数量：${this.messageHandlers.get(messageType)!.length}`)
  }

  off(messageType: string, handler: (message: string) => void) {
    const handlers = this.messageHandlers.get(messageType)
    if (handlers) {
      const oldLen = handlers.length
      const index = handlers.indexOf(handler)
      if (index > -1) {
        handlers.splice(index, 1)
        console.log(`[SSE] off() 移除 handler 成功：${messageType}, 移除前=${oldLen}, 移除后=${handlers.length}`)
      } else {
        console.warn(`[SSE] off() 未找到 handler: ${messageType}, 当前 handler 数量=${oldLen}`)
      }
    } else {
      console.warn(`[SSE] off() 没有该类型的 handler: ${messageType}`)
    }
  }

  private triggerHandlers(messageType: string, message: SSEMessage) {
    console.log(`[SSE] triggerHandlers: 查找 messageType="${messageType}"`)
    console.log(`[SSE] triggerHandlers: 已注册的 handler 类型列表:`, Array.from(this.messageHandlers.keys()))
    
    const handlers = this.messageHandlers.get(messageType)
    if (handlers) {
      console.log(`[SSE] triggerHandlers: ✅ 找到 messageType="${messageType}", handler 数量=${handlers.length}`)
      handlers.forEach(handler => {
        try {
          handler(message)
        } catch (error) {
          console.error('SSE 消息处理器执行失败:', error)
        }
      })
    } else {
      console.warn(`[SSE] triggerHandlers: ❌ 未找到 messageType="${messageType}" 的 handler`)
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

  getMessagesByType(type: string): SSEMessage[] {
    return this.messages.value.filter(msg => msg.type === type)
  }

  clearMessages() {
    this.messages.value = []
  }
}

export const sseService = new SSEService()
export default sseService