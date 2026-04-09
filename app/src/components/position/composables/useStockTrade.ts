import { ref, reactive } from 'vue'
import axios from 'axios'
import { message } from '@/tool'
import sseService from '@/ts/SSEService'

export interface StockPosition {
  code: string
  exchange: string
  name: string
  quantity: number
  available: number
  costPrice: number
  currentPrice: number
  profit: number
}

export interface StockOrder {
  id: number
  code: string
  name: string
  type: string
  orderType: string
  price: number
  quantity: number
  status: string
  sysID?: string
  timestamp: number
}

export interface NewStockOrder {
  code: string
  name: string
  tradeType: string
  orderType: string
  price: string
  curPrice: string
  quantity: string
  conditionType: string
  triggerPrice: string
  stopPrice: string
  validity: string
  validityDate: string
}

export function useStockTrade() {
  const stockPositions = ref<StockPosition[]>([])
  const stockOrders = ref<StockOrder[]>([])
  const stockTradingEnabled = ref(true)
  const totalProfit = ref(0)

  // 订单状态映射
  const getOrderStatusType = (type: number): string => {
    const statusMap: Record<number, string> = {
      1: 'pending',
      2: 'rejected',
      3: 'part_filled',
      4: 'filled',
      5: 'fail',
      6: 'cancel_part',
      7: 'cancelled',
      8: 'pending',
      9: 'cancelled_filled',
      11: 'privilege'
    }
    return statusMap[type] || 'pending'
  }

  const getOrderStatusText = (status: string): string => {
    const textMap: Record<string, string> = {
      pending: '待成交',
      filled: '已成交',
      cancelled: '已撤单',
      rejected: '交易所已拒绝',
      fail: '已失败',
      privilege: '无权限'
    }
    return textMap[status] || '监控中'
  }

  const getOrderType = (orgType: number): string => {
    const typeMap: Record<number, string> = {
      0: 'market',
      1: 'limit',
      2: 'conditional',
      3: 'stop'
    }
    return typeMap[orgType] || 'limit'
  }

  const getOrderTypeDisplayText = (orderType: string): string => {
    const textMap: Record<string, string> = {
      limit: '限价',
      market: '市价',
      conditional: '条件',
      stop: '止损'
    }
    return textMap[orderType] || orderType
  }

  const getOrderTypeText = (type: string): string => {
    const textMap: Record<string, string> = {
      buy: '普通买入',
      buy_margin: '融资买入',
      sell: '普通卖出',
      sell_short: '融券卖出'
    }
    return textMap[type] || type
  }

  const getOrderTypeClass = (type: string): string => {
    if (type === 'buy' || type === 'buy_margin') return 'order-type-buy'
    if (type === 'sell' || type === 'sell_short') return 'order-type-sell'
    return ''
  }

  // 获取交易所
  const getExchange = (id: string): string => {
    const tokens = id.split('.')
    switch (tokens[0]) {
      case 'sz': return '深交所'
      case 'sh': return '上交所'
      case 'bj': return '北交所'
      default: return tokens[0]
    }
  }

  // 格式化时间
  const formatTime = (timestamp: number): string => {
    if (!timestamp) return '-'
    const date = new Date(timestamp * 1000)
    return `${date.getFullYear()}-${String(date.getMonth() + 1).padStart(2, '0')}-${String(date.getDate()).padStart(2, '0')} ${String(date.getHours()).padStart(2, '0')}:${String(date.getMinutes()).padStart(2, '0')}:${String(date.getSeconds()).padStart(2, '0')}`
  }

  // SSE 事件处理
  const onPositionUpdate = (messageData: any) => {
    const data = JSON.parse(messageData['data'])
    stockPositions.value = []
    let totalProfitValue: number = 0

    for (const item of data) {
      const profit = (parseFloat(item.curPrice) - parseFloat(item.price)) * parseFloat(item.quantity)
      stockPositions.value.push({
        code: item.id,
        exchange: getExchange(item.id),
        name: item.name,
        quantity: parseFloat(item.quantity),
        costPrice: parseFloat(item.price),
        currentPrice: parseFloat(item.curPrice),
        available: parseFloat(item.valid_quantity),
        profit: profit
      })
      totalProfitValue += profit
    }
    totalProfit.value = totalProfitValue
  }

  const onOrderUpdate = (messageData: any) => {
    const data = JSON.parse(messageData['data'])
    stockOrders.value = []

    for (const item of data) {
      stockOrders.value.push({
        id: Date.now(),
        code: item.id,
        name: item.name,
        type: item.direct === 0 ? 'buy' : 'sell',
        orderType: getOrderType(item.orderType),
        price: parseFloat(item.price),
        quantity: parseFloat(item.quantity),
        sysID: item.sysID,
        timestamp: item.timestamp,
        status: getOrderStatusType(item.status)
      })
    }
  }

  const onTradeReport = (messageData: any) => {
    const item = messageData
    const order: StockOrder = {
      id: Date.now(),
      code: item.symbol,
      name: item.name,
      type: item.direct === '0' ? 'buy' : 'sell',
      orderType: getOrderType(parseInt(item.orderType)),
      price: item.price,
      quantity: item.quantity,
      sysID: item.sysID,
      timestamp: item.timestamp,
      status: getOrderStatusType(parseInt(item.status))
    }
    stockOrders.value.push(order)
  }

  // API 调用
  const submitOrder = async (order: any) => {
    try {
      const res = await axios.post('/v0/trade/order', order)
      return { success: true, sysID: res.data.sysID }
    } catch (error) {
      console.error('订单提交失败:', error)
      return { success: false, error }
    }
  }

  const cancelOrder = async (sysID: string) => {
    try {
      const params = { sysID: [sysID], type: 0 }
      const result = await axios.delete('/v0/trade/order', { data: params })
      if (result.status !== 200) {
        throw new Error('撤销订单失败')
      }
      return { success: true }
    } catch (error) {
      console.error('撤单失败:', error)
      return { success: false, error }
    }
  }

  const cancelAllOrders = async () => {
    const pendingOrders = stockOrders.value.filter(o => o.status === 'pending' || o.status === 'waiting')
    if (pendingOrders.length === 0) {
      return { success: true, count: 0 }
    }

    const delIds = pendingOrders.map(o => o.sysID).filter(Boolean) as string[]
    if (delIds.length === 0) {
      return { success: true, count: 0 }
    }

    try {
      const params = { sysID: delIds, type: 0 }
      await axios.delete('/v0/trade/order', { data: params })
      return { success: true, count: delIds.length }
    } catch (error) {
      console.error('一键撤单失败:', error)
      return { success: false, error }
    }
  }

  const toggleTrading = async () => {
    try {
      const params = { type: 0, operation: stockTradingEnabled.value ? 0 : 1 }
      const response = await axios.put('/v0/trade/order', params)
      if (response.status !== 200) {
        throw new Error(response.data.message)
      }
      stockTradingEnabled.value = !stockTradingEnabled.value
      return { success: true, enabled: stockTradingEnabled.value }
    } catch (error) {
      console.error('切换报单状态失败:', error)
      return { success: false, error }
    }
  }

  const queryAllOrders = async () => {
    try {
      const response = await axios.get('/v0/trade/order', { params: { type: 0 } })
      if (!response.data) return

      stockOrders.value = response.data.map((item: any) => ({
        id: item.id,
        code: item.symbol,
        name: item.name,
        type: item.direct === 0 ? 'buy' : 'sell',
        price: item.prices[0],
        quantity: item.quantity,
        orderType: getOrderType(item.orderType),
        status: item.status === 1 ? 'pending' : item.status === 3 ? 'waiting' : 'filled',
        sysID: item.sysID,
        timestamp: item.timestamp
      }))
    } catch (error) {
      console.error('查询订单失败:', error)
    }
  }

  // 注册 SSE 监听
  const registerSSE = () => {
    sseService.on('update_position', onPositionUpdate)
    sseService.on('update_order', onOrderUpdate)
    sseService.on('order_success', () => {})
    sseService.on('trade_report', onTradeReport)
  }

  // 卸载 SSE 监听
  const unregisterSSE = () => {
    sseService.off('update_position', onPositionUpdate)
    sseService.off('update_order', onOrderUpdate)
    sseService.off('order_success', () => {})
    sseService.off('trade_report', onTradeReport)
  }

  return {
    // 状态
    stockPositions,
    stockOrders,
    stockTradingEnabled,
    totalProfit,
    // 方法
    submitOrder,
    cancelOrder,
    cancelAllOrders,
    toggleTrading,
    queryAllOrders,
    registerSSE,
    unregisterSSE,
    // 工具函数
    getOrderStatusText,
    getOrderTypeDisplayText,
    getOrderTypeText,
    getOrderTypeClass,
    getExchange,
    formatTime
  }
}
