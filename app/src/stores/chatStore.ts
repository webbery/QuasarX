import { defineStore } from 'pinia'
import { ref } from 'vue'
import { useQuoteStore } from './quoteStore'
import { useAccountStore } from './account'
import { fetchSectorQuotes, calculateTotalAdvanceDecline } from '@/lib/sectorApi'

export interface ThoughtStep {
  id: string
  content: string        // 思考内容（灰体字）
  toolName?: string      // 调用的工具名称
  timestamp: number
}

export interface TokenUsage {
  promptTokens: number   // 输入 token 数
  completionTokens: number  // 输出 token 数
  totalTokens: number    // 总 token 数
}

export interface ChatMessage {
  id: string
  role: 'user' | 'assistant' | 'system'
  content: string
  timestamp: number
  thoughts?: ThoughtStep[]  // 思考步骤
  tokenUsage?: TokenUsage   // Token 使用量
}

export const useChatStore = defineStore('chat', () => {
  const visible = ref(false)
  const messages = ref<ChatMessage[]>([])
  const isLoading = ref(false)
  const marketContext = ref('')
  // 是否使用 LangGraph 多 Agent 模式（默认启用）
  const useLangGraph = ref(true)
  
  // 最大对话轮数限制
  const MAX_MESSAGES = 500
  // localStorage 保存的最近消息数（避免存储过大）
  const STORAGE_MESSAGES = 100

  // 从 localStorage 加载配置
  function loadFromStorage() {
    try {
      const stored = localStorage.getItem('quasarx_chat')
      if (stored) {
        const parsed = JSON.parse(stored)
        if (parsed.visible !== undefined) visible.value = parsed.visible
        if (parsed.messages) messages.value = parsed.messages
      }
    } catch (e) {
      console.warn('[Chat] 加载本地配置失败', e)
    }
  }

  // 保存配置到 localStorage
  function saveToStorage() {
    try {
      const data = {
        visible: visible.value,
        messages: messages.value.slice(-STORAGE_MESSAGES), // 只保存最近消息
      }
      localStorage.setItem('quasarx_chat', JSON.stringify(data))
    } catch (e) {
      console.warn('[Chat] 保存本地配置失败', e)
    }
  }

  // 切换显示/隐藏
  function toggle() {
    visible.value = !visible.value
    if (visible.value && messages.value.length === 0) {
      injectMarketContext()
      addGreeting()
    }
    saveToStorage()
  }

  // 设置可见性
  function setVisible(val: boolean) {
    visible.value = val
    if (val && messages.value.length === 0) {
      injectMarketContext()
      addGreeting()
    }
    saveToStorage()
  }

  // 添加消息
  function addMessage(msg: Omit<ChatMessage, 'id' | 'timestamp'> & { id?: string }): ChatMessage {
    const message: ChatMessage = {
      ...msg,
      id: msg.id ?? (Date.now().toString() + Math.random().toString(36).slice(2, 8)),
      timestamp: Date.now(),
    }
    messages.value.push(message)
    // 限制消息历史长度到 500 轮
    if (messages.value.length > MAX_MESSAGES) {
      messages.value = messages.value.slice(-MAX_MESSAGES)
    }
    saveToStorage()
    // 返回响应式代理（数组中的最后一个元素）
    return messages.value[messages.value.length - 1]
  }

  // 清空消息
  function clearMessages() {
    messages.value = []
    saveToStorage()
  }

  // 添加问候语
  function addGreeting() {
  }

  /**
   * 获取最近 N 条对话历史
   * @param count 获取数量
   * @returns 对话历史数组
   */
  function getConversationHistory(count: number = 20): ChatMessage[] {
    return messages.value.slice(-count)
  }

  /**
   * 压缩对话历史为摘要
   * 当对话过长时调用 LLM 生成摘要
   * @param llmCallback LLM 回调函数
   * @returns 摘要文本
   */
  async function summarizeConversation(
    llmCallback: (prompt: string) => Promise<string>
  ): Promise<string> {
    if (messages.value.length <= 20) {
      // 消息不多，不需要压缩
      return ''
    }

    // 提取需要压缩的部分（前 80% 的消息）
    const compressCount = Math.floor(messages.value.length * 0.8)
    const toCompress = messages.value.slice(0, compressCount)
    
    if (toCompress.length < 10) {
      // 太少，不需要压缩
      return ''
    }

    try {
      const conversationText = toCompress
        .map(m => `${m.role === 'user' ? '用户' : '助手'}: ${m.content}`)
        .join('\n\n')

      const summaryPrompt = `请将以下对话历史压缩为简洁的摘要，保留关键信息、上下文和重要决定。摘要应能帮助理解之前的对话内容：

${conversationText}

请用简洁的语言概括上述对话的核心内容和上下文。摘要：`

      const summary = await llmCallback(summaryPrompt)
      
      // 移除已压缩的消息，只保留最近的
      messages.value = messages.value.slice(compressCount)
      saveToStorage()
      
      console.log(`[ChatStore] 对话已压缩：${toCompress.length} 条 → 摘要`)
      return summary
    } catch (error) {
      console.error('[ChatStore] 对话摘要生成失败:', error)
      // 失败时不移除消息，返回空摘要
      return ''
    }
  }

  // 注入当日行情背景
  async function injectMarketContext() {
    try {
      const quoteStore = useQuoteStore()
      const accountStore = useAccountStore()

      // 订阅主要指数
      quoteStore.subscribe('SH000001')  // 上证指数
      quoteStore.subscribe('SH000300')  // 沪深300

      // 等待行情数据加载
      await quoteStore.fetchAll()

      // 获取指数数据
      const shIndex = quoteStore.getQuote('SH000001')
      const hs300 = quoteStore.getQuote('SH000300')

      // 获取板块数据
      let sectorText = ''
      try {
        const sectors = await fetchSectorQuotes()
        const { totalUp, totalDown } = calculateTotalAdvanceDecline(sectors)

        // 排序获取领涨/跌板块
        const sorted = [...sectors].sort((a, b) => b.change_pct - a.change_pct)
        const topGainers = sorted.slice(-3).reverse()
        const topLosers = sorted.slice(0, 3)

        sectorText = `\n• 市场广度：上涨 ${totalUp} 家，下跌 ${totalDown} 家`

        if (topGainers.length > 0) {
          sectorText += `\n• 领涨板块：${topGainers.map(s => `${s.name}(+${s.change_pct.toFixed(2)}%)`).join('、')}`
        }
        if (topLosers.length > 0) {
          sectorText += `\n• 领跌板块：${topLosers.map(s => `${s.name}(${s.change_pct.toFixed(2)}%)`).join('、')}`
        }
      } catch (e) {
        console.warn('[Chat] 获取板块数据失败', e)
      }

      // 构建账户风险数据
      let accountRiskText = ''
      if (accountStore.maxDrawDown !== 0 || accountStore.sharpRatio !== 0 || accountStore.todayPL !== 0) {
        accountRiskText = `
【账户风险状况】
• 最大回撤：${accountStore.maxDrawDown.toFixed(2)}%
• 夏普比率：${accountStore.sharpRatio.toFixed(2)}
• 今日盈亏：¥${accountStore.todayPL.toLocaleString()}`
      }

      // 构建市场风险数据（暂缺，待接入真实数据源）
      let marketRiskText = ''

      // 构建策略风险概览（暂缺，待接入真实数据源）
      let strategyRiskText = ''
      
      // 格式化日期
      const today = new Date()
      const dateStr = `${today.getFullYear()}-${String(today.getMonth() + 1).padStart(2, '0')}-${String(today.getDate()).padStart(2, '0')}`
      
      // 构建行情概览（作为系统上下文）
      marketContext.value = `【当前市场背景 ${dateStr}】
${shIndex.lastPrice > 0 ? `上证指数：${shIndex.lastPrice.toFixed(2)} 点 (${shIndex.changePct >= 0 ? '+' : ''}${shIndex.changePct.toFixed(2)}%)` : ''}
${hs300.lastPrice > 0 ? `沪深300：${hs300.lastPrice.toFixed(2)} 点 (${hs300.changePct >= 0 ? '+' : ''}${hs300.changePct.toFixed(2)}%)` : ''}
${sectorText}
${accountRiskText}
${marketRiskText}
${strategyRiskText}

请在回答时参考以上市场数据和风险指标，提供与当前市场环境相关的建议和分析。`.trim()
    } catch (e) {
      console.warn('[Chat] 注入行情数据失败', e)
      marketContext.value = ''
    }
  }

  // 加载配置
  loadFromStorage()

  return {
    visible,
    messages,
    isLoading,
    marketContext,
    useLangGraph,
    toggle,
    setVisible,
    addMessage,
    clearMessages,
    addGreeting,
    getConversationHistory,
    summarizeConversation,
  }
})
