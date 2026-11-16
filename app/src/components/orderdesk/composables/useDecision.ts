import { ref, computed } from 'vue'
import axios from 'axios'
import sseService from '@/ts/SSEService'

export enum DecisionAction {
  OpenLong = 'open_long',
  CloseLong = 'close_long',
  OpenShort = 'open_short',
  CloseShort = 'close_short',
}

export const actionLabels: Record<DecisionAction, string> = {
  [DecisionAction.OpenLong]: '买入开多',
  [DecisionAction.CloseLong]: '卖出平多',
  [DecisionAction.OpenShort]: '卖出开空',
  [DecisionAction.CloseShort]: '买入平空',
}

export const actionColors: Record<DecisionAction, string> = {
  [DecisionAction.OpenLong]: '#ef4444',
  [DecisionAction.CloseLong]: '#22c55e',
  [DecisionAction.OpenShort]: '#22c55e',
  [DecisionAction.CloseShort]: '#ef4444',
}

export interface DecisionItem {
  id: number
  strategy: string
  symbol: string
  action: DecisionAction
  label: string
  quantity: number
  price: number
  epoch: number
  timestamp: number
  /** 归属日 "YYYY-MM-DD"：这批决策对应的交易日（旧版后端不返回） */
  date?: string
  executed: boolean
  executedQuantity: number
  executedPrice: number
  closed: boolean
}

export function useDecision() {
  const decisions = ref<DecisionItem[]>([])
  const selectedId = ref<number | null>(null)
  /** 当前决策批次的归属日（后端回传，"YYYY-MM-DD"） */
  const decisionDate = ref<string>('')

  const selectedDecision = computed(() =>
    decisions.value.find(d => d.id === selectedId.value) ?? null
  )

  const pendingDecisions = computed(() =>
    decisions.value.filter(d => !d.executed && !d.closed)
  )

  /**
   * 拉取决策列表。
   * 不传 date → 后端返回最新归属日的一批决策。日终决策按归属日（被回放的交易日）
   * 落库，与今天差一天是常态，固定查今天会把上一交易日的待下单决策整批漏掉。
   * 传 date → 查指定归属日（历史查询用）。
   */
  const fetchDecisions = async (date?: string) => {
    try {
      const res = await axios.get('/v0/trade/decisions', date ? { params: { date } } : {})
      const list: DecisionItem[] = Array.isArray(res.data) ? res.data : []
      decisions.value = list
      decisionDate.value = list[0]?.date ?? ''
    } catch (error) {
      console.error('[Decision] fetch failed:', error)
    }
  }

  const executeDecision = async (id: number, quantity: number, price: number) => {
    const d = decisions.value.find(d => d.id === id)
    if (!d || d.executed) return { success: false, error: '决策已执行' }

    try {
      const isBuy = d.action === DecisionAction.OpenLong || d.action === DecisionAction.CloseShort
      await axios.post('/v0/trade/order', {
        symbol: d.symbol,
        direct: isBuy ? 0 : 1,
        quantity,
        prices: price,          // 服务端 OrderHandler 读取的是 prices
        type: 1,
        decisionId: id,
        strategy: d.strategy    // 触发 OrderHandler 手动回写路径（Fix #4）
      })
      d.executed = true
      d.executedQuantity = quantity
      d.executedPrice = price
      return { success: true }
    } catch (error: any) {
      console.error('[Decision] execute failed:', error)
      return { success: false, error: error.response?.data?.error || '下单失败' }
    }
  }

  const closeDecision = async (id: number) => {
    const d = decisions.value.find(d => d.id === id)
    if (!d) return { success: false, error: '决策不存在' }
    if (d.executed) return { success: false, error: '决策已下单，无法关闭' }
    if (d.closed) return { success: true }
    try {
      await axios.put('/v0/trade/decisions', { id })
      d.closed = true
      return { success: true }
    } catch (error: any) {
      console.error('[Decision] close failed:', error)
      return { success: false, error: error.response?.data?.error || '关闭订单失败' }
    }
  }

  const onManualDecision = (messageData: any) => {
    try {
      const payload = JSON.parse(messageData.payload || messageData.data)
      // SSE 带的是归属日（as_of），与 REST 返回的 date 同一口径
      if (payload.as_of) {
        decisionDate.value = payload.as_of
      }
      if (payload.decisions && Array.isArray(payload.decisions)) {
        for (const d of payload.decisions) {
          const exists = decisions.value.find(item => item.id === d.id)
          if (!exists) {
            decisions.value.push({
              id: d.id,
              strategy: payload.strategy || '',
              symbol: d.symbol,
              action: d.action as DecisionAction,
              label: d.label || actionLabels[d.action as DecisionAction] || '',
              quantity: d.quantity,
              price: d.price,
              epoch: d.epoch,
              timestamp: Math.floor(Date.now() / 1000),
              date: payload.as_of || undefined,
              executed: false,
              executedQuantity: 0,
              executedPrice: 0,
              closed: false
            })
          }
        }
      }
    } catch (e) {
      console.error('[Decision] SSE parse error:', e)
    }
  }

  const registerSSE = () => {
    sseService.on('manual_decision', onManualDecision)
  }

  const unregisterSSE = () => {
    sseService.off('manual_decision', onManualDecision)
  }

  return {
    decisions,
    selectedId,
    selectedDecision,
    pendingDecisions,
    decisionDate,
    fetchDecisions,
    executeDecision,
    closeDecision,
    registerSSE,
    unregisterSSE,
    actionLabels,
    actionColors,
  }
}
