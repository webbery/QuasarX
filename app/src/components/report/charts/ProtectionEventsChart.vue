<!-- app/src/components/report/charts/ProtectionEventsChart.vue -->
<!-- 风控保护事件展示 - 止损/止盈/追踪/时间/公式触发记录 -->

<template>
  <div class="chart-card full-width">
    <div class="chart-title">
      <div class="title-icon">🛡️</div>
      <span>Protection Events</span>
    </div>

    <!-- 无事件状态 -->
    <div v-if="events.length === 0" class="empty-state">
      <div class="empty-icon">🛡️</div>
      <div>暂无风控触发事件</div>
      <div class="empty-hint">在策略图中添加 Protection 节点并启用止损/止盈后，触发记录将在此展示</div>
    </div>

    <template v-else>
      <!-- 汇总栏 -->
      <div class="summary-bar">
        <div class="summary-item">
          <span class="summary-label">Total</span>
          <span class="summary-value">{{ events.length }}</span>
        </div>
        <div class="summary-item" v-for="t in typeList" :key="t.key">
          <span class="summary-label">{{ t.label }}</span>
          <span class="summary-value" :class="t.colorClass">{{ countByType(t.key) }}</span>
        </div>
        <div class="summary-item">
          <span class="summary-label">Avg Loss</span>
          <span class="summary-value loss">{{ avgLossPct }}%</span>
        </div>
      </div>

      <!-- 事件表格 -->
      <div class="event-table">
        <div class="event-header">
          <span class="col-date">Date</span>
          <span class="col-symbol">Symbol</span>
          <span class="col-type">Type</span>
          <span class="col-price col-num">Entry</span>
          <span class="col-price col-num">Exit</span>
          <span class="col-pnl col-num">P&L</span>
        </div>
        <div
          v-for="(evt, i) in events"
          :key="i"
          class="event-row"
        >
          <span class="col-date">{{ formatDate(evt.datetime) }}</span>
          <span class="col-symbol mono">{{ evt.symbol }}</span>
          <span class="col-type">
            <span class="type-badge" :class="typeClass(evt.type)">{{ typeLabel(evt.type) }}</span>
          </span>
          <span class="col-price col-num mono">{{ evt.entry_price.toFixed(2) }}</span>
          <span class="col-price col-num mono">{{ evt.current_price.toFixed(2) }}</span>
          <span class="col-pnl col-num mono" :class="pnlClass(evt)">
            {{ pnlPct(evt) }}%
          </span>
        </div>
      </div>
    </template>
  </div>
</template>

<script setup lang="ts">
import { computed } from 'vue'
import type { ProtectionEvent } from '@/stores/history'

const props = defineProps<{
  events: ProtectionEvent[]
}>()

const typeList = [
  { key: 'stop_loss', label: 'SL', colorClass: 'loss' },
  { key: 'take_profit', label: 'TP', colorClass: 'profit' },
  { key: 'trailing_stop', label: 'Trail', colorClass: 'loss' },
  { key: 'time_stop', label: 'Time', colorClass: 'warn' },
  { key: 'formula_stop', label: 'Formula', colorClass: 'formula' },
] as const

function countByType(type: string): number {
  return props.events.filter(e => e.type === type).length
}

const avgLossPct = computed(() => {
  if (props.events.length === 0) return '0.00'
  const total = props.events.reduce((sum, e) => {
    return sum + ((e.current_price - e.entry_price) / e.entry_price) * 100
  }, 0)
  return (total / props.events.length).toFixed(2)
})

function pnlPct(evt: ProtectionEvent): string {
  if (evt.entry_price <= 0) return '0.00'
  return ((evt.current_price - evt.entry_price) / evt.entry_price * 100).toFixed(2)
}

function pnlClass(evt: ProtectionEvent): string {
  const pct = (evt.current_price - evt.entry_price) / evt.entry_price
  if (pct > 0) return 'profit'
  if (pct < 0) return 'loss'
  return ''
}

function formatDate(ts: number): string {
  if (!ts) return '-'
  const d = new Date(ts * 1000)
  const m = String(d.getMonth() + 1).padStart(2, '0')
  const day = String(d.getDate()).padStart(2, '0')
  return `${d.getFullYear()}-${m}-${day}`
}

function typeLabel(type: string): string {
  const map: Record<string, string> = {
    stop_loss: '止损',
    take_profit: '止盈',
    trailing_stop: '追踪止损',
    time_stop: '时间止损',
    formula_stop: '公式止损',
  }
  return map[type] || type
}

function typeClass(type: string): string {
  const map: Record<string, string> = {
    stop_loss: 'badge-sl',
    take_profit: 'badge-tp',
    trailing_stop: 'badge-ts',
    time_stop: 'badge-time',
    formula_stop: 'badge-formula',
  }
  return map[type] || ''
}
</script>

<style scoped>
.chart-card {
  background: var(--card-bg, #1a1f2e);
  border-radius: 12px;
  padding: 20px;
  border: 1px solid var(--border, #2a3449);
  box-shadow: 0 4px 6px rgba(0, 0, 0, 0.1);
  display: flex;
  flex-direction: column;
  min-height: 0;
}

.chart-title {
  display: flex;
  align-items: center;
  gap: 8px;
  margin-bottom: 12px;
  font-size: 15px;
  font-weight: 600;
  color: #e0e0e0;
}
.title-icon { font-size: 18px; }

.summary-bar {
  display: flex;
  gap: 16px;
  padding: 10px 16px;
  background: rgba(41, 98, 255, 0.05);
  border: 1px solid var(--border, #2a3449);
  border-radius: 8px;
  margin-bottom: 16px;
  flex-wrap: wrap;
}
.summary-item {
  display: flex;
  flex-direction: column;
  align-items: center;
  min-width: 60px;
}
.summary-label {
  font-size: 10px;
  color: #a0aec0;
  text-transform: uppercase;
  letter-spacing: 0.5px;
}
.summary-value {
  font-size: 14px;
  font-weight: 600;
  color: #e0e0e0;
  margin-top: 2px;
}
.summary-value.profit { color: #00c853; }
.summary-value.loss { color: #ff1744; }
.summary-value.warn { color: #ff9100; }
.summary-value.formula { color: #aa66ff; }

.empty-state {
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  height: 160px;
  color: #a0aec0;
  gap: 8px;
}
.empty-icon { font-size: 36px; opacity: 0.5; }
.empty-hint { font-size: 12px; opacity: 0.6; }

/* 事件表格 */
.event-table {
  border: 1px solid var(--border, #2a3449);
  border-radius: 8px;
  overflow: hidden;
  font-size: 12px;
}
.event-header {
  display: grid;
  grid-template-columns: 90px 1fr 90px 80px 80px 70px;
  padding: 8px 12px;
  background: rgba(41, 98, 255, 0.08);
  border-bottom: 1px solid var(--border, #2a3449);
  color: #a0aec0;
  font-weight: 600;
  text-transform: uppercase;
  letter-spacing: 0.4px;
  font-size: 10px;
}
.event-row {
  display: grid;
  grid-template-columns: 90px 1fr 90px 80px 80px 70px;
  padding: 8px 12px;
  align-items: center;
  border-bottom: 1px solid rgba(42, 52, 73, 0.4);
  color: #e0e0e0;
  transition: background 0.15s ease;
}
.event-row:last-child { border-bottom: none; }
.event-row:hover { background: rgba(41, 98, 255, 0.06); }

.col-num { text-align: right; }
.mono { font-family: 'SF Mono', Menlo, Consolas, monospace; }

.col-date { color: #a0aec0; }
.col-symbol {
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
  min-width: 0;
}

/* 类型 badge */
.type-badge {
  display: inline-block;
  padding: 2px 6px;
  border-radius: 4px;
  font-size: 10px;
  font-weight: 600;
  line-height: 1.4;
}
.badge-sl { background: rgba(255, 23, 68, 0.15); color: #ff1744; }
.badge-tp { background: rgba(0, 200, 83, 0.15); color: #00c853; }
.badge-ts { background: rgba(255, 145, 0, 0.15); color: #ff9100; }
.badge-time { background: rgba(41, 98, 255, 0.15); color: #2962ff; }
.badge-formula { background: rgba(170, 102, 255, 0.15); color: #aa66ff; }

.profit { color: #00c853; }
.loss { color: #ff1744; }
</style>
