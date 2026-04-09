<template>
  <div class="order-list">
    <div class="pane-header">
      <h3>股票委托</h3>
      <div class="action-buttons">
        <button
          class="btn"
          :class="tradingEnabled ? 'btn-warning' : 'btn-success'"
          @click="handleToggleTrading"
        >
          {{ tradingEnabled ? '暂停报单' : '允许报单' }}
        </button>
        <button class="btn btn-warning" @click="handleCancelAll">一键撤单</button>
      </div>
    </div>

    <table class="order-table">
      <thead>
        <tr>
          <th>代码</th>
          <th>名称</th>
          <th>委托类型</th>
          <th>订单类型</th>
          <th>价格</th>
          <th>数量</th>
          <th>状态</th>
          <th>委托时间</th>
          <th>操作</th>
        </tr>
      </thead>
      <tbody>
        <tr v-for="order in orders" :key="order.id" class="order-row">
          <td>{{ order.code }}</td>
          <td>{{ order.name }}</td>
          <td :class="getOrderTypeClass(order.type)">{{ getOrderTypeText(order.type) }}</td>
          <td>
            <span class="order-type-badge" :class="`order-type-${order.orderType}`">
              {{ getOrderTypeDisplayText(order.orderType) }}
            </span>
          </td>
          <td>{{ order.price }}</td>
          <td>{{ order.quantity }}</td>
          <td>
            <span class="order-status" :class="`status-${order.status}`">
              {{ getOrderStatusText(order.status) }}
            </span>
          </td>
          <td>{{ formatTime(order.timestamp) }}</td>
          <td>
            <button
              class="btn btn-mini btn-danger"
              @click="handleCancelOrder(order)"
              v-if="order.status === 'pending' || order.status === 'waiting'"
            >
              撤单
            </button>
            <span v-else class="no-action">-</span>
          </td>
        </tr>
      </tbody>
    </table>

    <div class="empty-state" v-if="orders.length === 0">
      暂无委托记录
    </div>
  </div>
</template>

<script setup lang="ts">
import type { StockOrder } from './composables/useStockTrade'

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

const getOrderTypeText = (type: string): string => {
  const textMap: Record<string, string> = {
    buy: '普通买入',
    buy_margin: '融资买入',
    sell: '普通卖出',
    sell_short: '融券卖出'
  }
  return textMap[type] || type
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

const getOrderTypeClass = (type: string): string => {
  if (type === 'buy' || type === 'buy_margin') return 'order-type-buy'
  if (type === 'sell' || type === 'sell_short') return 'order-type-sell'
  return ''
}

const props = defineProps({
  orders: { 
    type: Array as () => Array<{
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
    }>, 
    required: true 
  },
  tradingEnabled: { type: Boolean, default: true }
})

const emit = defineEmits<{
  cancelOrder: [order: any]
  cancelAll: []
  toggleTrading: []
}>()

const formatTime = (timestamp: number): string => {
  if (!timestamp) return '-'
  const date = new Date(timestamp * 1000)
  return `${date.getFullYear()}-${String(date.getMonth() + 1).padStart(2, '0')}-${String(date.getDate()).padStart(2, '0')} ${String(date.getHours()).padStart(2, '0')}:${String(date.getMinutes()).padStart(2, '0')}:${String(date.getSeconds()).padStart(2, '0')}`
}

const handleCancelOrder = (order: any) => {
  emit('cancelOrder', order)
}

const handleCancelAll = () => {
  emit('cancelAll')
}

const handleToggleTrading = () => {
  emit('toggleTrading')
}
</script>

<style scoped>
.order-list {
  margin-top: 30px;
  border: 1px solid var(--border);
  border-radius: 6px;
  overflow: hidden;
}

.pane-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 15px 20px;
  background-color: rgba(0, 0, 0, 0.1);
  border-bottom: 1px solid var(--border);
}

.pane-header h3 {
  margin: 0;
  font-size: 1.1rem;
  font-weight: 600;
}

.action-buttons {
  display: flex;
  gap: 10px;
}

.order-table {
  width: 100%;
  border-collapse: collapse;
}

.order-table th,
.order-table td {
  padding: 12px 15px;
  text-align: left;
  border-bottom: 1px solid var(--border);
}

.order-table th {
  color: var(--text-secondary);
  font-weight: 500;
  font-size: 0.85rem;
  background-color: rgba(0, 0, 0, 0.05);
}

.order-table tbody tr:hover {
  background-color: rgba(255, 255, 255, 0.03);
}

.order-status {
  padding: 4px 8px;
  border-radius: 4px;
  font-size: 0.75rem;
  font-weight: 500;
}

.status-pending {
  color: #ff9800;
}

.status-waiting {
  color: #2196f3;
}

.status-filled {
  color: var(--secondary);
}

.status-cancelled {
  color: #757575;
}

.status-fail,
.status-rejected {
  color: #d32f2f;
}

.order-type-buy {
  color: var(--secondary);
}

.order-type-sell {
  color: #f44336;
}

.order-type-badge {
  padding: 2px 6px;
  border-radius: 3px;
  font-size: 0.75rem;
  font-weight: 500;
}

.order-type-limit {
  background-color: rgba(41, 98, 255, 0.2);
  color: #2962ff;
}

.order-type-market {
  background-color: rgba(0, 200, 83, 0.2);
  color: #00c853;
}

.order-type-conditional {
  background-color: rgba(255, 109, 0, 0.2);
  color: #ff6d00;
}

.order-type-stop {
  background-color: rgba(244, 67, 54, 0.2);
  color: #f44336;
}

.btn {
  padding: 8px 16px;
  border-radius: 4px;
  cursor: pointer;
  font-size: 0.85rem;
  border: none;
  color: white;
  transition: all 0.2s;
}

.btn-mini {
  padding: 4px 8px;
  font-size: 0.75rem;
}

.btn-warning {
  background: linear-gradient(90deg, #ff9800, #f57c00);
}

.btn-success {
  background: linear-gradient(90deg, #4caf50, #388e3c);
}

.btn-danger {
  background: linear-gradient(90deg, #f44336, #d32f2f);
}

.btn:hover:not(:disabled) {
  transform: translateY(-1px);
}

.empty-state {
  text-align: center;
  padding: 20px;
  color: var(--text-secondary);
}

.no-action {
  color: var(--text-secondary);
}
</style>
