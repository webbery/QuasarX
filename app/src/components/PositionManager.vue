<template>
  <div class="position-manager">
    <!-- 市场持仓面板 -->
    <div class="card market-positions">
      <div class="card-header">
        <h2>持仓管理</h2>
        <span>
          <span v-if="activeMarketTab === 'stock'">股票</span>
          <span v-else-if="activeMarketTab === 'option'">期权</span>
          <span v-else-if="activeMarketTab === 'future'">期货</span>
          总盈亏：{{ accountStore.todayPL.toFixed(4) }}
        </span>

        <div class="tab-navigation">
          <div
            class="tab-item"
            :class="{ active: activeMarketTab === 'stock' }"
            @click="activeMarketTab = 'stock'"
          >
            股票持仓
          </div>
        </div>
      </div>

      <div class="card-content">
        <!-- 股票持仓 -->
        <StockPositionPanel
          :active="activeMarketTab === 'stock'"
          :positions="stockTrade.stockPositions.value"
          @trade="handleStockTrade"
        />

        <StockOrderPanel
          v-if="activeMarketTab === 'stock'"
          :orders="stockTrade.stockOrders.value"
          :trading-enabled="stockTrade.stockTradingEnabled.value"
          @cancel-order="handleCancelOrder"
          @cancel-all="handleCancelAllOrders"
          @toggle-trading="handleToggleTrading"
        />

        <!-- 期权持仓 -->
        <OptionPositionPanel
          :active-market-tab="activeMarketTab === 'option'"
          :positions="optionPositions"
          :orders="optionOrders"
          :settings="optionSettings"
          @trade="handleOptionTrade"
          @cancel-order="handleCancelOrder"
          @save-settings="handleSaveOptionSettings"
        />

        <!-- 期货持仓 -->
        <FuturePositionPanel
          :active="activeMarketTab === 'future'"
          :positions="futurePositions"
          :orders="futureOrders"
          @trade="handleFutureTrade"
          @cancel-order="handleCancelOrder"
        />
      </div>
    </div>

    <!-- 确认对话框 -->
    <ConfirmModal
      v-model:visible="modalVisible"
      :title="modalTitle"
      :message="modalMessage"
      @confirm="handleModalConfirm"
    />
  </div>
</template>

<script setup lang="ts">
import { ref, reactive, onMounted, onUnmounted, provide } from 'vue'
import { useAccountStore } from '@/stores/account'
import { useStockTrade } from '@/components/position/composables/useStockTrade'
import StockPositionPanel from './position/StockPositionPanel.vue'
import StockOrderPanel from './position/StockOrderPanel.vue'
import OptionPositionPanel from './position/OptionPositionPanel.vue'
import FuturePositionPanel from './position/FuturePositionPanel.vue'
import ConfirmModal from './position/ConfirmModal.vue'
import { message } from '@/tool'
import sseService from '@/ts/SSEService'

const accountStore = useAccountStore()
const stockTrade = useStockTrade()

// 市场选项卡
const activeMarketTab = ref<'stock' | 'option' | 'future'>('stock')

// 模态框状态
const modalVisible = ref(false)
const modalTitle = ref('')
const modalMessage = ref('')
const modalCallback = ref<(() => void) | null>(null)

// 期权数据（暂时保留硬编码，后续可抽离为 composable）
const optionPositions = ref([
  { code: '10002467', name: '50ETF 购 3 月 3500', quantity: 10, available: 10, costPrice: 0.0850, currentPrice: 0.0920, profit: 70 },
  { code: '10002468', name: '50ETF 沽 3 月 3500', quantity: 5, available: 5, costPrice: 0.0450, currentPrice: 0.0380, profit: -35 }
])

const optionOrders = ref([
  { id: 1, code: '10002467', name: '50ETF 购 3 月 3500', type: 'buyOpen', typeText: '买开', price: 0.0910, quantity: 5, status: 'pending' }
])

const optionSettings = reactive({
  positionRatio: '0.8',
  speedLimit: '100',
  selfTradeCheck: true
})

// 期货数据（暂时保留硬编码，后续可抽离为 composable）
const futurePositions = ref([
  { code: 'IF2403', name: '沪深 300 指数期货 2403', direction: 'long', quantity: 2, available: 2, openPrice: 3850.5, currentPrice: 3902.3, profit: 10360 },
  { code: 'IC2403', name: '中证 500 指数期货 2403', direction: 'short', quantity: 1, available: 1, openPrice: 5820.0, currentPrice: 5756.8, profit: 6320 }
])

const futureOrders = ref([
  { id: 1, code: 'IF2403', name: '沪深 300 指数期货 2403', type: 'open', direction: 'long', price: 3895.0, quantity: 1, status: 'pending' }
])

// 提供给子组件的方法
const handleSecuritySelection = (data: any) => {
  console.log('handleSecuritySelection:', data)
  // TODO: 打开交易面板
}

provide('handleSecuritySelection', handleSecuritySelection)

// 模态框方法
const showModal = (title: string, msg: string, callback: () => void) => {
  modalTitle.value = title
  modalMessage.value = msg
  modalCallback.value = callback
  modalVisible.value = true
}

const handleModalConfirm = () => {
  if (modalCallback.value) {
    modalCallback.value()
  }
}

// 股票交易处理
const handleStockTrade = (position: any, operationType: string) => {
  console.log('股票交易:', position, operationType)
  // TODO: 打开交易面板
  message.info(`打开 ${position.name} 的${operationType}面板`)
}

// 期权交易处理
const handleOptionTrade = (position: any, operationType: string) => {
  console.log('期权交易:', position, operationType)
  message.info(`打开 ${position.name} 的${operationType}面板`)
}

// 期货交易处理
const handleFutureTrade = (position: any, operationType: string) => {
  console.log('期货交易:', position, operationType)
  message.info(`打开 ${position.name} 的${operationType}面板`)
}

// 撤单处理
const handleCancelOrder = async (order: any) => {
  const info = `${order.name}, 价格：${order.price}`
  showModal('撤单确认', `确定要撤销此订单吗？${info}`, async () => {
    const result = await stockTrade.cancelOrder(order.sysID)
    if (result.success) {
      message.success('撤销订单请求已发出')
    } else {
      message.error('撤销订单失败')
    }
  })
}

// 一键撤单处理
const handleCancelAllOrders = async () => {
  showModal('一键撤单确认', '确定要撤销所有待成交订单吗？', async () => {
    const result = await stockTrade.cancelAllOrders()
    if (result.success) {
      message.success(`已撤销 ${result.count} 个订单`)
    } else {
      message.error('一键撤单失败')
    }
  })
}

// 切换交易开关
const handleToggleTrading = async () => {
  const action = stockTrade.stockTradingEnabled.value ? '暂停' : '允许'
  showModal(`${action}报单确认`, `确定要${action}股票报单功能吗？${action === '暂停' ? '暂停后无法提交新的订单，但可以撤单。' : ''}`, async () => {
    const result = await stockTrade.toggleTrading()
    if (result.success) {
      message.success(result.enabled ? '报单功能已启用' : '报单功能已暂停')
    } else {
      message.error('操作失败')
    }
  })
}

// 保存期权设置
const handleSaveOptionSettings = () => {
  message.success('期权参数设置已保存')
}

// SSE 事件处理
const onOrderSuccess = (messageData: any) => {
  console.log('订单成功:', messageData)
}

onMounted(() => {
  // 注册 SSE 监听
  stockTrade.registerSSE()
  sseService.on('order_success', onOrderSuccess)
  
  // 首次查询当前订单
  stockTrade.queryAllOrders()
})

onUnmounted(() => {
  // 清理 SSE 监听
  stockTrade.unregisterSSE()
  sseService.off('order_success', onOrderSuccess)
})
</script>

<style scoped>
.position-manager {
  background-color: var(--dark-bg);
  color: var(--text);
  padding: 5px;
  font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
}

.card {
  background-color: var(--panel-bg);
  border-radius: 8px;
  border: 1px solid var(--border);
  margin-bottom: 5px;
  overflow: hidden;
}

.card-header {
  border-bottom: 1px solid var(--border);
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 15px 20px;
}

.card-header h2 {
  margin: 0;
  font-size: 1.2rem;
  font-weight: 600;
}

.card-content {
  padding: 20px;
}

.tab-navigation {
  display: flex;
  gap: 5px;
}

.tab-item {
  padding: 8px 16px;
  cursor: pointer;
  border-radius: 4px;
  font-size: 0.9rem;
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
</style>
