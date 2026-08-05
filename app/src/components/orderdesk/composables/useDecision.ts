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
  executed: boolean
  executedQuantity: number
  executedPrice: number
}

export function useDecision() {
  const decisions = ref<DecisionItem[]>([])
  const selectedId = ref<number | null>(null)

  const selectedDecision = computed(() =>
    decisions.value.find(d => d.id === selectedId.value) ?? null
  )

  const pendingDecisions = computed(() =>
    decisions.value.filter(d => !d.executed)
  )

  const formatDate = (date: Date): string => {
    const y = date.getFullYear()
    const m = String(date.getMonth() + 1).padStart(2, '0')
    const d = String(date.getDate()).padStart(2, '0')
    return `${y}-${m}-${d}`
  }

  const fetchDecisions = async () => {
    try {
      const res = await axios.get('/v0/trade/decisions', {
        params: { date: formatDate(new Date()) }
      })
      decisions.value = res.data
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
        price,
        type: 1,
        decisionId: id
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

  const onManualDecision = (messageData: any) => {
    try {
      const payload = JSON.parse(messageData.payload || messageData.data)
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
              executed: false,
              executedQuantity: 0,
              executedPrice: 0
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
    fetchDecisions,
    executeDecision,
    registerSSE,
    unregisterSSE,
    actionLabels,
    actionColors,
  }
}
