// app/src/stores/shiborStore.ts
// SHIBOR 利率数据 - 单一定时器，多组件共享

import { defineStore } from 'pinia'
import axios from 'axios'
import { aiContextBuilder } from '@/lib/AIContextBuilder'

export interface ShiborItem {
  date: string
  term: string
  rate: number
  change: number
}

export interface ShiborMap {
  [key: string]: ShiborItem
}

const POLL_INTERVAL = 60_000 // 1 分钟（SHIBOR 每天只更新一次）

export const useShiborStore = defineStore('shibor', {
  state: () => ({
    data: {} as ShiborMap,
    loading: false,
    lastUpdate: 0 as number,
    timer: null as ReturnType<typeof setInterval> | null,
    subscribers: new Set<string>(),
  }),

  getters: {
    /**
     * 获取指定期限的 SHIBOR 数据
     */
    getTerm: (state) => (term: string): ShiborItem => {
      return state.data[term] || { date: '', term, rate: 0, change: 0 }
    },

    /**
     * 获取所有期限数据
     */
    getAllTerms: (state): ShiborItem[] => {
      return Object.values(state.data)
    },

    /**
     * 数据日期
     */
    dataDate: (state): string => {
      const first = Object.values(state.data)[0]
      return first?.date || ''
    },

    /**
     * 是否有有效数据
     */
    hasData: (state): boolean => {
      return Object.keys(state.data).length > 0
    },
  },

  actions: {
    /**
     * 订阅 SHIBOR 数据更新
     * 组件调用此方法后，store 会开始定时拉取数据
     */
    subscribe(componentId: string) {
      this.subscribers.add(componentId)
      this._ensureTimer()
    },

    /**
     * 取消订阅
     */
    unsubscribe(componentId: string) {
      this.subscribers.delete(componentId)
      this._stopTimerIfIdle()
    },

    /**
     * 从后端获取 SHIBOR 数据
     */
    async fetch() {
      if (this.subscribers.size === 0) return
      this.loading = true

      try {
        const response = await axios.get('/v0/market/shibor')
        
        if (response.data.status !== 'success') {
          throw new Error(response.data.message || '获取 SHIBOR 数据失败')
        }

        // 转换为 Map 格式，以 term 为 key
        const newData: ShiborMap = {}
        for (const item of response.data.data) {
          newData[item.term] = {
            date: item.date,
            term: item.term,
            rate: item.rate,
            change: item.change,
          }
        }

        this.data = newData
        this.lastUpdate = Date.now()

        console.log(`[shiborStore] 更新成功: ${response.data.date}, ${response.data.count} 条`)
        
        // 注册到 AI 数据源
        this._registerToAI()
      } catch (e: any) {
        console.error('[shiborStore] 获取 SHIBOR 数据失败:', e.message)
      } finally {
        this.loading = false
      }
    },

    _ensureTimer() {
      if (this.timer) return
      
      // 立即获取一次
      this.fetch()
      
      // 定时更新
      this.timer = setInterval(() => this.fetch(), POLL_INTERVAL)
      
      console.log('[shiborStore] 开始定时更新')
    },

    _stopTimerIfIdle() {
      if (this.subscribers.size > 0 || !this.timer) return

      clearInterval(this.timer)
      this.timer = null

      console.log('[shiborStore] 停止定时更新（无订阅者）')
    },
    
    _registerToAI() {
      const data = Object.values(this.data)
      
      aiContextBuilder.registry.register({
        id: 'data:shibor',
        name: 'SHIBOR利率',
        description: '上海银行间同业拆借利率',
        tags: ['shibor', 'interest_rate', 'macro', 'finance'],
        getter: () => data,
        updateFrequency: '每日',
        aiContext: '包含日期、期限（隔夜/1周/2周等）、利率、变化值'
      })
    },
  },
})
