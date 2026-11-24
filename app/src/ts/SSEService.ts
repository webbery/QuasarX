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

  /** 重连退避参数 */
  private retryCount = 0
  private readonly retryDelay = 1000
  private readonly maxRetryDelay = 30000
  private readonly maxRetries = 10

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
    // 有 remote（登录时记下的后端地址）就直连它；没有就用同源相对路径 ——
    // 浏览器测试台走 vite 代理转发 /v0，相对路径才连得上（直连会撞跨域+自签证书）
    const server = localStorage.getItem('remote')
    const url = server ? `https://${server}/v0/server/event` : '/v0/server/event'
    try {
      await fetchEventSource(url, {
        method: 'GET',
        headers: {
          'Authorization': `${token}`,
          'Content-Type': 'text/event-stream',
          'Cache-Control': 'no-cache'
        },
        signal: this.abortController.signal,
        // 不注册库内置的 visibilitychange 监听器。该监听器在页面由隐藏变回可见时
        // 会 abort 当前连接并重建：Electron 里窗口被遮挡/切走就会翻转 document.hidden，
        // 一天能触发几十次，每次都留下一条服务端尚未回收的订阅，重连窗口内的事件还会
        // 被 inproc pub/sub 直接丢弃。SSE 本就该在后台持续收消息，保持连接不重建。
        openWhenHidden: true,

        onopen: async (response: any) => {
          this.isConnected.value = true
          this.retryCount = 0
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
          // 断线后必须重连：终态事件（done/failed）只在连接期间推一次，
          // 错过就永远收不到 —— 进度卡片会一直转圈。
          // 返回间隔让库自动重连；抛异常则彻底停止重试。
          if (this.retryCount >= this.maxRetries) {
            console.error('[SSE] 重连次数用尽，放弃')
            throw error
          }
          this.retryCount++
          const delay = Math.min(this.retryDelay * this.retryCount, this.maxRetryDelay)
          console.warn(`[SSE] ${delay}ms 后重连（第 ${this.retryCount}/${this.maxRetries} 次）`)
          return delay
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

      // 存整条消息（含事件名）而不是裸 data —— 否则按事件名回放/过滤都做不到，
      // getMessagesByType 会永远返回空数组。
      this.messages.value.push(message)
      this.lastMessage.value = message

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

  /**
   * 订阅某类事件，返回取消函数。
   *
   * 与 on/off 的区别：handler 直接拿到 data（不用再拆 message.data），
   * 且返回 unsubscribe —— UI 产物挂载/卸载时成对调用，不易漏。
   */
  onEvent(eventType: string, handler: (data: any, message: SSEMessage) => void): () => void {
    const wrapper = (message: SSEMessage) => handler(message.data, message)
    this.on(eventType, wrapper as unknown as (message: string) => void)
    return () => this.off(eventType, wrapper as unknown as (message: string) => void)
  }

  /**
   * 回放缓存里某类事件。
   *
   * 用在哪：进度卡挂载时，服务端的 started/progress 早就发完了 ——
   * 不回放的话卡片会停在初始态，直到下一条事件到达。
   * 缓存只保留最近 50 条，足够覆盖「产物创建 → 组件挂载」这段窗口。
   */
  replay(eventType: string, handler: (data: any, message: SSEMessage) => void): void {
    for (const message of this.messages.value) {
      if (message.type === eventType) handler(message.data, message)
    }
  }

  // 其他方法保持不变...
  on(messageType: string, handler: (message: string) => void) {
    if (!this.messageHandlers.has(messageType)) {
      this.messageHandlers.set(messageType, [])
    }
    const handlers = this.messageHandlers.get(messageType)!
    // 同一 handler 重复注册会让每条 SSE 事件被重复处理（进度计数成倍虚高），
    // 而 off() 只摘掉第一个实例，补注册救不回来 —— 这里直接挡掉
    if (handlers.includes(handler)) {
      console.warn(`[SSE] on() handler 已存在，跳过重复注册: ${messageType}`)
      return
    }
    handlers.push(handler)
    console.log(`[SSE] on() 注册 handler: ${messageType}, 当前 handler 数量：${handlers.length}`)
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
    this.retryCount = 0
    // 不清 messageHandlers：handler 的生命周期归组件管（onMounted 注册、
    // onUnmounted 注销），服务单方面 clear 会把它们抹掉且没人补注册 ——
    // 第二次登录走 connect() → disconnect() 时，所有面板的 handler 就此失联，
    // 行情下载进度条会永久停在旧值不动。
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