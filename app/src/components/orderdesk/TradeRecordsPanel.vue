<!-- app/src/components/orderdesk/TradeRecordsPanel.vue -->
<!-- 交易明细记录面板 - 从「历史复盘」迁移而来，展示策略的成交明细与持仓周期明细 -->

<script setup lang="ts">
import { ref, computed, watch } from 'vue'
import { useStrategyTrades } from '@/composables/useStrategyTrades'

interface Props {
  /** 当前追踪的策略名；为空时提示先选择策略 */
  strategyName?: string
}

const props = defineProps<Props>()

type RecordTab = 'trades' | 'holdings'
const activeTab = ref<RecordTab>('trades')

const { trades, holdings, loading, error, fetchTrades, reset } = useStrategyTrades()

const sortedTrades = computed(() =>
  [...trades.value].sort((a, b) => a.timestamp.localeCompare(b.timestamp))
)

const sortedHoldings = computed(() =>
  [...holdings.value].sort((a, b) => a.buyDate.localeCompare(b.buyDate))
)

const totalPnl = computed(() =>
  trades.value.reduce((sum, t) => sum + (t.pnl ?? 0), 0)
)

// 策略切换 → 重新拉取；清空策略 → 重置
watch(
  () => props.strategyName,
  (name) => {
    if (name) {
      fetchTrades(name)
    } else {
      reset()
    }
  },
  { immediate: true }
)

function getReturnClass(value: number): string {
  return value >= 0 ? 'return-positive' : 'return-negative'
}

function downloadCSV(lines: string[], filename: string) {
  const blob = new Blob(['\uFEFF' + lines.join('\n')], { type: 'text/csv;charset=utf-8;' })
  const url = URL.createObjectURL(blob)
  const a = document.createElement('a')
  a.href = url
  a.download = filename
  a.click()
  URL.revokeObjectURL(url)
}

/** 导出当前子标签的数据 */
function exportCSV() {
  const stamp = new Date().toISOString().split('T')[0]
  const tag = props.strategyName || 'strategy'

  if (activeTab.value === 'trades') {
    if (sortedTrades.value.length === 0) return
    const lines = ['时间,标的,方向,价格,数量,金额,佣金,盈亏']
    sortedTrades.value.forEach((t) => {
      lines.push(
        `${t.timestamp},${t.symbol},${t.direction === 'buy' ? '买入' : '卖出'},${t.price},${t.quantity},${t.amount},${t.commission},${t.pnl !== undefined ? t.pnl : ''}`
      )
    })
    downloadCSV(lines, `trades_${tag}_${stamp}.csv`)
    return
  }

  if (sortedHoldings.value.length === 0) return
  const lines = ['标的,买入日期,卖出日期,买入价,卖出价,数量,持仓天数,盈亏,收益率(%)']
  sortedHoldings.value.forEach((h) => {
    lines.push(
      `${h.symbol},${h.buyDate},${h.sellDate},${h.buyPrice},${h.sellPrice},${h.quantity},${h.holdingDays},${h.pnl},${h.pnlPercent}`
    )
  })
  downloadCSV(lines, `holdings_${tag}_${stamp}.csv`)
}

function refresh() {
  if (props.strategyName) fetchTrades(props.strategyName)
}
</script>

<template>
  <div class="trade-records-panel">
    <!-- 面板标题栏 -->
    <div class="panel-header">
      <h3>交易明细记录</h3>
      <span v-if="strategyName" class="strategy-badge">{{ strategyName }}</span>
      <span v-if="strategyName" class="record-summary">
        {{ sortedTrades.length }} 笔成交 · 累计盈亏
        <span :class="getReturnClass(totalPnl)">
          {{ totalPnl >= 0 ? '+' : '' }}¥{{ totalPnl.toFixed(2) }}
        </span>
      </span>
      <div class="header-actions">
        <button
          class="icon-btn"
          :disabled="!strategyName || loading"
          title="刷新交易记录"
          @click="refresh"
        >
          <i :class="loading ? 'fas fa-spinner fa-spin' : 'fas fa-sync-alt'"></i>
        </button>
        <button
          class="export-btn"
          :disabled="activeTab === 'trades' ? sortedTrades.length === 0 : sortedHoldings.length === 0"
          @click="exportCSV"
        >
          <i class="fas fa-download"></i> 导出CSV
        </button>
      </div>
    </div>

    <!-- 子标签 -->
    <div class="sub-tabs">
      <button :class="{ active: activeTab === 'trades' }" @click="activeTab = 'trades'">
        交易记录
        <span class="tab-count">{{ sortedTrades.length }}</span>
      </button>
      <button :class="{ active: activeTab === 'holdings' }" @click="activeTab = 'holdings'">
        持仓周期
        <span class="tab-count">{{ sortedHoldings.length }}</span>
      </button>
    </div>

    <!-- 内容区 -->
    <div class="records-body">
      <!-- 未选择策略 -->
      <div v-if="!strategyName" class="empty-state">
        <i class="fas fa-hand-pointer"></i>
        <p>请选择策略</p>
        <span>在上方选择追踪策略后查看成交明细</span>
      </div>

      <!-- 加载错误 -->
      <div v-else-if="error" class="empty-state error">
        <i class="fas fa-exclamation-triangle"></i>
        <p>加载失败</p>
        <span>{{ error }}</span>
      </div>

      <!-- 成交记录 -->
      <div v-else-if="activeTab === 'trades'" class="table-wrap">
        <table v-if="sortedTrades.length > 0" class="records-table">
          <thead>
            <tr>
              <th>时间</th>
              <th>标的</th>
              <th>方向</th>
              <th>价格</th>
              <th>数量</th>
              <th>金额</th>
              <th>佣金</th>
              <th>盈亏</th>
            </tr>
          </thead>
          <tbody>
            <tr v-for="trade in sortedTrades" :key="trade.id">
              <td class="mono">{{ trade.timestamp }}</td>
              <td class="mono symbol-cell">{{ trade.symbol }}</td>
              <td>
                <span :class="trade.direction === 'buy' ? 'badge-buy' : 'badge-sell'">
                  {{ trade.direction === 'buy' ? '买入' : '卖出' }}
                </span>
              </td>
              <td class="mono">¥{{ trade.price.toFixed(2) }}</td>
              <td class="mono">{{ trade.quantity }}</td>
              <td class="mono">¥{{ trade.amount.toFixed(2) }}</td>
              <td class="mono muted">¥{{ trade.commission.toFixed(2) }}</td>
              <td class="mono" :class="trade.pnl !== undefined ? getReturnClass(trade.pnl) : 'muted'">
                {{ trade.pnl !== undefined ? `¥${trade.pnl.toFixed(2)}` : '-' }}
              </td>
            </tr>
          </tbody>
        </table>
        <div v-else class="empty-state">
          <i class="fas fa-inbox"></i>
          <p>暂无成交记录</p>
          <span>该策略尚未产生任何交易</span>
        </div>
      </div>

      <!-- 持仓周期明细 -->
      <div v-else class="table-wrap">
        <table v-if="sortedHoldings.length > 0" class="records-table">
          <thead>
            <tr>
              <th>标的</th>
              <th>买入日期</th>
              <th>卖出日期</th>
              <th>买入价</th>
              <th>卖出价</th>
              <th>数量</th>
              <th>持仓天数</th>
              <th>盈亏</th>
              <th>收益率</th>
            </tr>
          </thead>
          <tbody>
            <tr v-for="h in sortedHoldings" :key="h.symbol + h.buyDate">
              <td class="mono symbol-cell">{{ h.symbol }}</td>
              <td class="mono">{{ h.buyDate }}</td>
              <td class="mono">{{ h.sellDate }}</td>
              <td class="mono">¥{{ h.buyPrice.toFixed(2) }}</td>
              <td class="mono">¥{{ h.sellPrice.toFixed(2) }}</td>
              <td class="mono">{{ h.quantity }}</td>
              <td class="mono">{{ h.holdingDays }} 天</td>
              <td class="mono" :class="getReturnClass(h.pnl)">¥{{ h.pnl.toFixed(2) }}</td>
              <td class="mono" :class="getReturnClass(h.pnlPercent)">
                {{ h.pnlPercent >= 0 ? '+' : '' }}{{ h.pnlPercent.toFixed(2) }}%
              </td>
            </tr>
          </tbody>
        </table>
        <div v-else class="empty-state">
          <i class="fas fa-inbox"></i>
          <p>暂无持仓周期记录</p>
          <span>该策略尚未产生已平仓的持仓</span>
        </div>
      </div>
    </div>
  </div>
</template>

<style scoped>
.trade-records-panel {
  display: flex;
  flex-direction: column;
  height: 100%;
  min-height: 0;
  background: rgba(15, 23, 42, 0.6);
}

.panel-header {
  padding: 10px 16px;
  border-bottom: 1px solid rgba(74, 158, 255, 0.15);
  display: flex;
  align-items: center;
  gap: 12px;
}

.panel-header h3 {
  margin: 0;
  font-size: 14px;
  font-weight: 600;
  color: #e2e8f0;
  white-space: nowrap;
}

.strategy-badge {
  padding: 2px 8px;
  border-radius: 4px;
  font-size: 12px;
  background: rgba(96, 165, 250, 0.15);
  border: 1px solid rgba(96, 165, 250, 0.3);
  color: #60a5fa;
  white-space: nowrap;
}

.record-summary {
  font-size: 12px;
  color: #64748b;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.header-actions {
  margin-left: auto;
  display: flex;
  gap: 8px;
  flex-shrink: 0;
}

.icon-btn {
  padding: 4px 9px;
  background: transparent;
  border: 1px solid rgba(74, 158, 255, 0.2);
  border-radius: 4px;
  color: #94a3b8;
  cursor: pointer;
  transition: all 0.2s;
  font-size: 12px;
}

.icon-btn:hover:not(:disabled) {
  background: rgba(74, 158, 255, 0.15);
  color: #60a5fa;
  border-color: rgba(74, 158, 255, 0.4);
}

.icon-btn:disabled {
  opacity: 0.4;
  cursor: not-allowed;
}

.export-btn {
  padding: 4px 10px;
  background: rgba(5, 150, 105, 0.2);
  border: 1px solid rgba(5, 150, 105, 0.4);
  border-radius: 4px;
  color: #34d399;
  font-size: 12px;
  cursor: pointer;
  transition: all 0.2s;
  display: flex;
  align-items: center;
  gap: 5px;
}

.export-btn:hover:not(:disabled) {
  background: rgba(5, 150, 105, 0.35);
}

.export-btn:disabled {
  opacity: 0.4;
  cursor: not-allowed;
}

/* === 子标签 === */

.sub-tabs {
  display: flex;
  gap: 4px;
  padding: 8px 16px 0;
  border-bottom: 1px solid rgba(74, 158, 255, 0.12);
}

.sub-tabs button {
  padding: 6px 14px;
  background: transparent;
  border: none;
  border-bottom: 2px solid transparent;
  color: #94a3b8;
  font-size: 13px;
  cursor: pointer;
  transition: all 0.2s;
  display: flex;
  align-items: center;
  gap: 6px;
}

.sub-tabs button:hover {
  color: #e2e8f0;
}

.sub-tabs button.active {
  color: #60a5fa;
  border-bottom-color: #60a5fa;
}

.tab-count {
  padding: 0 6px;
  border-radius: 8px;
  background: rgba(74, 158, 255, 0.15);
  font-size: 11px;
  font-family: 'SF Mono', 'Fira Code', monospace;
}

/* === 内容区 === */

.records-body {
  flex: 1;
  overflow: auto;
  min-height: 0;
}

.table-wrap {
  height: 100%;
  overflow: auto;
}

.records-table {
  width: 100%;
  border-collapse: collapse;
  font-size: 13px;
}

.records-table th {
  padding: 8px 12px;
  text-align: left;
  color: #64748b;
  font-weight: 500;
  font-size: 12px;
  position: sticky;
  top: 0;
  background: rgba(15, 23, 42, 0.95);
  border-bottom: 1px solid rgba(74, 158, 255, 0.15);
  white-space: nowrap;
  z-index: 1;
}

.records-table td {
  padding: 7px 12px;
  color: #cbd5e1;
  border-bottom: 1px solid rgba(255, 255, 255, 0.04);
  white-space: nowrap;
}

.records-table tbody tr:hover {
  background: rgba(74, 158, 255, 0.08);
}

.mono {
  font-family: 'SF Mono', 'Fira Code', monospace;
  font-size: 12px;
}

.muted {
  color: #64748b;
}

.symbol-cell {
  color: #e2e8f0;
}

/* === 买卖标签 === */

.badge-buy {
  display: inline-block;
  padding: 2px 8px;
  background: rgba(16, 185, 129, 0.2);
  color: #10b981;
  border-radius: 4px;
  font-size: 12px;
  font-weight: 600;
}

.badge-sell {
  display: inline-block;
  padding: 2px 8px;
  background: rgba(255, 23, 68, 0.2);
  color: #ff1744;
  border-radius: 4px;
  font-size: 12px;
  font-weight: 600;
}

/* === 盈亏着色 === */

.return-positive {
  color: #10b981;
  font-weight: 600;
}

.return-negative {
  color: #ff1744;
  font-weight: 600;
}

/* === 空状态 === */

.empty-state {
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  height: 100%;
  min-height: 140px;
  color: #475569;
  gap: 8px;
}

.empty-state i {
  font-size: 28px;
  opacity: 0.5;
}

.empty-state p {
  margin: 0;
  font-size: 14px;
  color: #94a3b8;
}

.empty-state span {
  font-size: 12px;
}

.empty-state.error i {
  color: #f87171;
  opacity: 0.7;
}
</style>
