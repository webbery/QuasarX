// app/src/stores/quoteStore.ts
// 统一行情数据管理 — 单一定时器，多组件共享

import { defineStore } from 'pinia'
import { fetchQuotes, convertSymbolToApiFormat, type QuoteData } from '@/lib/tickflow'
import { aiContextBuilder } from '@/lib/AIContextBuilder'

// TickFlow 返回的原始行情结构
interface QuoteResult {
  code?: number
  msg?: string
  data?: QuoteData[]
}

export interface QuoteState {
  lastPrice: number
  changePct: number       // 已经是百分比值（不乘100）
  changeAmount: number
  timestamp: number
  high: number
  low: number
  open: number
  volume: number
}

function emptyQuote(): QuoteState {
  return {
    lastPrice: 0,
    changePct: 0,
    changeAmount: 0,
    timestamp: 0,
    high: 0,
    low: 0,
    open: 0,
    volume: 0,
  }
}

const POLL_INTERVAL = 10_000 // 10 秒

export const useQuoteStore = defineStore('quote', {
  state: () => ({
    quotes: new Map<string, QuoteState>(),
    subscribers: new Set<string>(),
    timer: null as ReturnType<typeof setInterval> | null,
    loading: false,
  }),

  getters: {
    getQuote: (state) => (symbol: string): QuoteState => {
      return state.quotes.get(symbol) || emptyQuote()
    },
  },

  actions: {
    subscribe(symbol: string) {
      this.subscribers.add(symbol)
      if (!this.quotes.has(symbol)) {
        this.quotes.set(symbol, emptyQuote())
      }
      this._ensureTimer()
    },

    unsubscribe(symbol: string) {
      this.subscribers.delete(symbol)
      this._stopTimerIfIdle()
    },

    async fetchAll() {
      if (this.subscribers.size === 0) return
      this.loading = true

      const symbols = Array.from(this.subscribers)
      try {
        const apiSymbols = symbols.map((s) => convertSymbolToApiFormat(s))
        const params = new URLSearchParams({ symbols: apiSymbols.join(',') })

        const baseUrl = getApiBaseUrl()
        const apiKey = localStorage.getItem('tickflow_api_key') || ''
        const url = `${baseUrl}/v1/quotes?${params.toString()}`
        const headers: Record<string, string> = { Accept: 'application/json' }
        if (apiKey) headers['x-api-key'] = apiKey

        const res = await fetch(url, { headers })
        if (!res.ok) throw new Error(`TickFlow Quotes API 错误：${res.status} ${res.statusText}`)

        const result: QuoteResult = await res.json()
        if (result.code !== undefined && result.code !== 0) {
          throw new Error(`TickFlow Quotes API 错误：${result.msg || '未知错误'}`)
        }

        const quotes = result.data || []
        for (const q of quotes) {
          // 查找匹配的 symbol（API 返回的可能格式不同）
          const match = symbols.find((s) => {
            const apiFmt = convertSymbolToApiFormat(s)
            return apiFmt === q.symbol || s === q.symbol
          })
          if (match) {
            const ext = q.ext
            this.quotes.set(match, {
              lastPrice: q.last_price ?? 0,
              changePct: ext?.change_pct ?? 0,
              changeAmount: ext?.change_amount ?? 0,
              timestamp: q.timestamp ?? 0,
              high: q.high ?? 0,
              low: q.low ?? 0,
              open: q.open ?? 0,
              volume: q.volume ?? 0,
            })
          }
        }
        
        // 注册到 AI 数据源
        this._registerToAI()
      } catch (e: any) {
        console.warn('[quoteStore]', e.message)
      } finally {
        this.loading = false
      }
    },

    _ensureTimer() {
      if (this.timer) return
      this.fetchAll()
      this.timer = setInterval(() => this.fetchAll(), POLL_INTERVAL)
    },

    _stopTimerIfIdle() {
      if (this.subscribers.size > 0 || !this.timer) return
      clearInterval(this.timer)
      this.timer = null
    },
    
    _registerToAI() {
      // 将行情数据注册到 AI 数据源
      const quotesArray = Array.from(this.quotes.entries()).map(([symbol, quote]) => ({
        symbol,
        ...quote
      }))
      
      aiContextBuilder.registry.register({
        id: 'data:quotes',
        name: '实时行情',
        description: '股票实时报价和涨跌数据',
        tags: ['quote', 'market', 'price', 'realtime'],
        getter: () => quotesArray,
        updateFrequency: '10秒',
        aiContext: '包含股票代码、名称、最新价、涨跌幅、成交量、最高价、最低价、开盘价等字段'
      })
    },
  },
})

// 内联获取 API 基础 URL（避免和 tickflow.ts 循环依赖）
function getApiBaseUrl(): string {
  const key = localStorage.getItem('tickflow_api_key')
  if (!key) {
    return 'https://free-api.tickflow.org'
  }
  return 'https://api.tickflow.org'
}
