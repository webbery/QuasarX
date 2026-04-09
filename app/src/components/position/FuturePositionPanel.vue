<template>
  <div class="tab-pane" :class="{ active: active }">
    <!-- 期货持仓表格 -->
    <table class="position-table">
      <thead>
        <tr>
          <th>合约</th>
          <th>名称</th>
          <th>多空</th>
          <th>持仓数量</th>
          <th>可用数量</th>
          <th>开仓均价</th>
          <th>当前价</th>
          <th>浮动盈亏</th>
          <th>操作</th>
        </tr>
      </thead>
      <tbody>
        <tr 
          v-for="position in positions" 
          :key="position.code" 
          :class="position.direction === 'long' ? 'long-position' : 'short-position'"
        >
          <td>{{ position.code }}</td>
          <td>{{ position.name }}</td>
          <td :class="position.direction === 'long' ? 'direction-long' : 'direction-short'">
            {{ position.direction === 'long' ? '多头' : '空头' }}
          </td>
          <td>{{ position.quantity }}</td>
          <td>{{ position.available }}</td>
          <td>{{ position.openPrice }}</td>
          <td>{{ position.currentPrice }}</td>
          <td :class="position.profit > 0 ? 'profit-positive' : 'profit-negative'">
            {{ position.profit > 0 ? '+' : '' }}{{ position.profit }}
          </td>
          <td>
            <button class="btn btn-mini" @click="handleTrade(position, 'open')">开仓</button>
            <button class="btn btn-mini btn-danger" @click="handleTrade(position, 'close')">平仓</button>
          </td>
        </tr>
      </tbody>
    </table>

    <div class="empty-state" v-if="positions.length === 0">
      暂无期货持仓
    </div>

    <!-- 期货委托列表 -->
    <div class="order-list">
      <h3>期货委托</h3>
      <div class="order-item" v-for="order in orders" :key="order.id">
        <div class="order-info">
          <span>{{ order.code }} {{ order.name }}</span> |
          <span>{{ order.type === 'open' ? '开仓' : '平仓' }}</span> |
          <span :class="order.direction === 'long' ? 'direction-long' : 'direction-short'">
            {{ order.direction === 'long' ? '多头' : '空头' }}
          </span> |
          <span>{{ order.price }} × {{ order.quantity }}</span>
        </div>
        <div class="order-actions">
          <span class="order-status" :class="`status-${order.status}`">
            {{ getOrderStatusText(order.status) }}
          </span>
          <button 
            class="btn btn-mini" 
            @click="handleCancelOrder(order)" 
            v-if="order.status === 'pending'"
          >
            撤单
          </button>
        </div>
      </div>
      <div class="empty-state" v-if="orders.length === 0">
        暂无期货委托
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
interface FuturePosition {
  code: string
  name: string
  direction: string
  quantity: number
  available: number
  openPrice: number
  currentPrice: number
  profit: number
}

interface FutureOrder {
  id: number
  code: string
  name: string
  type: string
  direction: string
  price: number
  quantity: number
  status: string
}

defineProps({
  active: { type: Boolean, default: true },
  positions: { type: Array as () => FuturePosition[], default: () => [] },
  orders: { type: Array as () => FutureOrder[], default: () => [] }
})

const emit = defineEmits<{
  trade: [position: FuturePosition, operationType: string]
  cancelOrder: [order: FutureOrder]
}>()

const getOrderStatusText = (status: string): string => {
  const textMap: Record<string, string> = {
    pending: '待成交',
    filled: '已成交',
    cancelled: '已撤单'
  }
  return textMap[status] || status
}

const handleTrade = (position: FuturePosition, operationType: string) => {
  emit('trade', position, operationType)
}

const handleCancelOrder = (order: FutureOrder) => {
  emit('cancelOrder', order)
}
</script>

<style scoped>
.tab-pane {
  display: none;
}

.tab-pane.active {
  display: block;
}

.position-table {
  width: 100%;
  border-collapse: collapse;
  margin-bottom: 10px;
}

.position-table th,
.position-table td {
  padding: 12px 15px;
  text-align: left;
  border-bottom: 1px solid var(--border);
}

.position-table th {
  color: var(--text-secondary);
  font-weight: 500;
  font-size: 0.85rem;
}

.position-table tbody tr:hover {
  background-color: rgba(255, 255, 255, 0.03);
}

.long-position {
  background-color: rgba(0, 200, 83, 0.05);
}

.short-position {
  background-color: rgba(244, 67, 54, 0.05);
}

.direction-long {
  color: var(--secondary);
  font-weight: 500;
}

.direction-short {
  color: #f44336;
  font-weight: 500;
}

.btn {
  padding: 4px 8px;
  font-size: 0.75rem;
  margin-right: 5px;
  border-radius: 4px;
  cursor: pointer;
  border: none;
  background: linear-gradient(90deg, #2563eb, #1d4ed8);
  color: white;
  transition: all 0.2s;
}

.btn-mini {
  padding: 4px 8px;
  font-size: 0.75rem;
}

.btn:hover:not(:disabled) {
  transform: translateY(-1px);
}

.btn-danger {
  background: linear-gradient(90deg, #f44336, #d32f2f);
}

.profit-positive {
  color: var(--secondary);
}

.profit-negative {
  color: #f44336;
}

.order-list {
  margin-top: 20px;
  border: 1px solid var(--border);
  border-radius: 6px;
  overflow: hidden;
  padding: 15px;
}

.order-list h3 {
  margin: 0 0 15px 0;
  font-size: 1rem;
  color: var(--text);
}

.order-item {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 12px 15px;
  border-bottom: 1px solid var(--border);
}

.order-item:last-child {
  border-bottom: none;
}

.order-info {
  display: flex;
  gap: 10px;
  color: var(--text);
}

.order-actions {
  display: flex;
  align-items: center;
  gap: 10px;
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

.status-filled {
  color: var(--secondary);
}

.status-cancelled {
  color: #757575;
}

.empty-state {
  text-align: center;
  padding: 20px;
  color: var(--text-secondary);
}
</style>
