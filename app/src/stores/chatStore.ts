import { defineStore } from 'pinia'
import { ref } from 'vue'
import { useQuoteStore } from './quoteStore'
import { useAccountStore } from './account'
import { fetchSectorQuotes, calculateTotalAdvanceDecline } from '@/lib/sectorApi'
import { useMockRiskData } from '@/components/risk/hooks/useMockRiskData'

export interface ChatMessage {
  id: string
  role: 'user' | 'assistant' | 'system'
  content: string
  timestamp: number
}

export const useChatStore = defineStore('chat', () => {
  const visible = ref(false)
  const messages = ref<ChatMessage[]>([])
  const isLoading = ref(false)
  const marketContext = ref('')

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
        messages: messages.value.slice(-50), // 只保存最近 50 条消息
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
    saveToStorage()
  }

  // 清空消息
  function clearMessages() {
    messages.value = []
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

  // 注入当日行情背景
  async function injectMarketContext() {
    try {
      const quoteStore = useQuoteStore()
      const accountStore = useAccountStore()
      const { marketData, strategies } = useMockRiskData()
      
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
      
      // 构建市场风险数据
      let marketRiskText = ''
      const md = marketData.value
      if (md.volatility > 0 || md.sentimentIndex > 0) {
        marketRiskText = `
【市场风险】
• 波动率：${md.volatility.toFixed(2)}%
• 市场情绪：${md.sentimentLabel}（${md.sentimentIndex}/100）`
      }
      
      // 构建策略风险概览
      let strategyRiskText = ''
      if (strategies.value.length > 0) {
        const strategyLines = strategies.value.slice(0, 5).map(s => 
          `• ${s.name}：VaR ${s.var_95}% | 最大回撤 ${s.maxDrawdown}% | 风险等级：${s.riskLevel}`
        )
        strategyRiskText = `
【策略风险概览】
${strategyLines.join('\n')}`
      }
      
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
    toggle,
    setVisible,
    addMessage,
    clearMessages,
    addGreeting,
  }
})
