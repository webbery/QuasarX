<template>
  <div class="tab-pane" :class="{ active: activeMarketTab }">
    <div class="tab-container">
      <div class="tab-header">
        <div class="tab-item" :class="{ active: activeOptionTab === 'positions' }" @click="activeOptionTab = 'positions'">
          持仓
        </div>
        <div class="tab-item" :class="{ active: activeOptionTab === 'settings' }" @click="activeOptionTab = 'settings'">
          参数设置
        </div>
      </div>

      <div class="tab-content">
        <!-- 期权持仓表格 -->
        <div class="tab-pane" :class="{ active: activeOptionTab === 'positions' }">
          <table class="position-table">
            <thead>
              <tr>
                <th>代码</th>
                <th>名称</th>
                <th>持仓数量</th>
                <th>可用数量</th>
                <th>成本价</th>
                <th>当前价</th>
                <th>浮动盈亏</th>
                <th>操作</th>
              </tr>
            </thead>
            <tbody>
              <tr v-for="position in positions" :key="position.code">
                <td>{{ position.code }}</td>
                <td>{{ position.name }}</td>
                <td>{{ position.quantity }}</td>
                <td>{{ position.available }}</td>
                <td>{{ position.costPrice }}</td>
                <td>{{ position.currentPrice }}</td>
                <td :class="position.profit > 0 ? 'profit-positive' : 'profit-negative'">
                  {{ position.profit > 0 ? '+' : '' }}{{ position.profit }}
                </td>
                <td>
                  <button class="btn btn-mini" @click="handleTrade(position, 'buyOpen')">买开</button>
                  <button class="btn btn-mini" @click="handleTrade(position, 'sellOpen')">卖开</button>
                  <button class="btn btn-mini btn-danger" @click="handleTrade(position, 'buyClose')">买平</button>
                  <button class="btn btn-mini btn-danger" @click="handleTrade(position, 'sellClose')">卖平</button>
                </td>
              </tr>
            </tbody>
          </table>

          <div class="empty-state" v-if="positions.length === 0">
            暂无期权持仓
          </div>
        </div>

        <!-- 参数设置 -->
        <div class="tab-pane" :class="{ active: activeOptionTab === 'settings' }">
          <div class="form-row">
            <div class="form-group">
              <label>成交持仓比例控制</label>
              <input 
                type="text" 
                class="form-control" 
                v-model="localSettings.positionRatio" 
                placeholder="请输入比例"
              >
            </div>
            <div class="form-group">
              <label>程序化流速控制</label>
              <input 
                type="text" 
                class="form-control" 
                v-model="localSettings.speedLimit" 
                placeholder="请输入流速"
              >
            </div>
          </div>
          <div class="form-row">
            <div class="form-group">
              <div class="checkbox-group">
                <input 
                  type="checkbox" 
                  v-model="localSettings.selfTradeCheck" 
                  id="selfTradeCheck"
                >
                <label for="selfTradeCheck">自成交判断</label>
              </div>
            </div>
          </div>
          <button class="btn btn-primary" @click="handleSaveSettings">保存设置</button>
        </div>
      </div>
    </div>

    <!-- 期权委托列表 -->
    <div class="order-list">
      <h3>期权委托</h3>
      <div class="order-item" v-for="order in orders" :key="order.id">
        <div class="order-info">
          <span>{{ order.code }} {{ order.name }}</span> |
          <span>{{ order.typeText }}</span> |
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
        暂无期权委托
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, reactive, watch } from 'vue'

interface OptionPosition {
  code: string
  name: string
  quantity: number
  available: number
  costPrice: number
  currentPrice: number
  profit: number
}

interface OptionOrder {
  id: number
  code: string
  name: string
  type: string
  typeText: string
  price: number
  quantity: number
  status: string
}

interface OptionSettings {
  positionRatio: string
  speedLimit: string
  selfTradeCheck: boolean
}

const props = defineProps({
  activeMarketTab: { type: Boolean, default: false },
  positions: { type: Array as () => OptionPosition[], default: () => [] },
  orders: { type: Array as () => OptionOrder[], default: () => [] },
  settings: { type: Object as () => OptionSettings, default: () => ({
    positionRatio: '0.8',
    speedLimit: '100',
    selfTradeCheck: true
  })}
})

const emit = defineEmits<{
  trade: [position: OptionPosition, operationType: string]
  cancelOrder: [order: OptionOrder]
  saveSettings: [settings: OptionSettings]
}>()

const activeOptionTab = ref('positions')
const localSettings = reactive<OptionSettings>({ ...props.settings })

// 监听 settings 变化
watch(() => props.settings, (newSettings) => {
  Object.assign(localSettings, newSettings)
}, { deep: true })

const getOrderStatusText = (status: string): string => {
  const textMap: Record<string, string> = {
    pending: '待成交',
    filled: '已成交',
    cancelled: '已撤单'
  }
  return textMap[status] || status
}

const handleTrade = (position: OptionPosition, operationType: string) => {
  emit('trade', position, operationType)
}

const handleCancelOrder = (order: OptionOrder) => {
  emit('cancelOrder', order)
}

const handleSaveSettings = () => {
  emit('saveSettings', { ...localSettings })
}
</script>

<style scoped>
.tab-pane {
  display: none;
}

.tab-pane.active {
  display: block;
}

.tab-container {
  margin-bottom: 20px;
}

.tab-header {
  display: flex;
  border-bottom: 1px solid var(--border);
  margin-bottom: 15px;
}

.tab-item {
  padding: 8px 16px;
  cursor: pointer;
  transition: all 0.2s;
  color: var(--text-secondary);
}

.tab-item:hover {
  background-color: rgba(255, 255, 255, 0.05);
}

.tab-item.active {
  background-color: var(--primary);
  color: white;
}

.tab-content {
  padding: 0;
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

.btn-primary {
  background: linear-gradient(90deg, #2962ff, #1e40af);
}

.profit-positive {
  color: var(--secondary);
}

.profit-negative {
  color: #f44336;
}

.form-row {
  display: flex;
  gap: 15px;
  margin-bottom: 15px;
}

.form-group {
  flex: 1;
}

.form-group label {
  display: block;
  margin-bottom: 6px;
  font-size: 0.85rem;
  color: var(--text-secondary);
}

.form-control {
  width: 100%;
  padding: 8px 12px;
  background-color: var(--darker-bg);
  border: 1px solid var(--border);
  border-radius: 4px;
  color: var(--text);
  font-size: 0.9rem;
}

.form-control:focus {
  outline: none;
  border-color: var(--primary);
}

.checkbox-group {
  display: flex;
  align-items: center;
  gap: 8px;
}

.checkbox-group input {
  width: 16px;
  height: 16px;
  cursor: pointer;
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
