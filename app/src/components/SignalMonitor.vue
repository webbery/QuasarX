<template>
  <div class="signal-monitor">
    <!-- 顶部：策略选择 + 刷新 -->
    <div class="monitor-header">
      <div class="header-left">
        <select v-model="selectedStrategy" class="strategy-select">
          <option value="">-- 选择策略 --</option>
          <option v-for="s in strategies" :key="s.name" :value="s.name">
            {{ s.name }}
            <span v-if="s.running"> ●</span>
          </option>
        </select>
        <span v-if="selectedStrategy" class="strategy-status" :class="{ running: currentStrategy?.running }">
          {{ currentStrategy?.running ? '🟢 运行中' : '⚪ 已停止' }}
        </span>
      </div>
      <div class="header-right">
        <span v-if="lastRefresh" class="refresh-time">最后更新: {{ formatTime(lastRefresh) }}</span>
        <button class="btn-refresh" @click="refreshAll" :disabled="!selectedStrategy || loading">
          <i class="fas fa-sync-alt" :class="{ spinning: loading }"></i>
        </button>
      </div>
    </div>

    <!-- 未选择策略 -->
    <div v-if="!selectedStrategy" class="empty-state">
      <i class="fas fa-satellite-dish"></i>
      <p>选择策略查看信号监控</p>
      <span>展示持仓、交易记录和信号日志</span>
    </div>

    <template v-else>
      <!-- 账户概览 -->
      <div class="section account-overview" v-if="capital">
        <div class="overview-grid">
          <div class="overview-item">
            <span class="label">总资金</span>
            <span class="value">¥{{ formatNum(capital.totalAllocated + capital.totalAvailable) }}</span>
          </div>
          <div class="overview-item">
            <span class="label">可用资金</span>
            <span class="value">¥{{ formatNum(capital.totalAvailable) }}</span>
          </div>
          <div class="overview-item">
            <span class="label">持仓市值</span>
            <span class="value">¥{{ formatNum(totalMarketValue) }}</span>
          </div>
          <div class="overview-item">
            <span class="label">持仓盈亏</span>
            <span class="value" :class="{ positive: totalPnL > 0, negative: totalPnL < 0 }">
              {{ totalPnL > 0 ? '+' : '' }}¥{{ formatNum(totalPnL) }}
            </span>
          </div>
        </div>
      </div>

      <!-- 风控状态：断路器 + VaR -->
      <div class="section risk-section" :class="'risk-level-' + riskStatus.breaker_level" v-if="riskStatus">
        <h3 class="section-title">
          <i class="fas fa-shield-alt"></i> 风控状态
          <span class="breaker-badge" :class="'level-' + riskStatus.breaker_level">
            {{ breakerLabel }}
          </span>
        </h3>
        <div class="risk-grid">
          <!-- 回撤进度条 -->
          <div class="risk-item">
            <span class="label">当前回撤</span>
            <span class="value" :class="{ negative: riskStatus.current_drawdown > 0 }">
              -{{ (riskStatus.current_drawdown * 100).toFixed(2) }}%
            </span>
            <div class="progress-bar">
              <div class="progress-fill" :style="{ width: drawdownPct + '%' }"
                   :class="drawdownColorClass"></div>
              <div class="threshold-markers">
                <span class="marker l1" :style="{ left: (riskStatus.thresholds.level1 * 100) + '%' }" title="L1 警戒"></span>
                <span class="marker l2" :style="{ left: (riskStatus.thresholds.level2 * 100) + '%' }" title="L2 减仓"></span>
                <span class="marker l3" :style="{ left: (riskStatus.thresholds.level3 * 100) + '%' }" title="L3 熔断"></span>
              </div>
            </div>
            <div class="threshold-labels">
              <span>L1: {{ (riskStatus.thresholds.level1 * 100).toFixed(1) }}%</span>
              <span>L2: {{ (riskStatus.thresholds.level2 * 100).toFixed(1) }}%</span>
              <span>L3: {{ (riskStatus.thresholds.level3 * 100).toFixed(1) }}%</span>
            </div>
          </div>
          <!-- VaR -->
          <div class="risk-item">
            <span class="label">VaR ({{ (riskStatus.var_limit * 100).toFixed(0) }}%上限)</span>
            <span class="value" :class="{ negative: riskStatus.var_breached }">
              {{ (riskStatus.var_ratio * 100).toFixed(2) }}%
            </span>
            <div class="progress-bar">
              <div class="progress-fill var-fill" :style="{ width: varPct + '%' }"
                   :class="{ breached: riskStatus.var_breached }"></div>
            </div>
          </div>
          <!-- 解除熔断按钮 -->
          <div class="risk-item" v-if="riskStatus.breaker_tripped">
            <span class="label">熔断已触发</span>
            <button class="btn-reset-breaker" @click="resetBreaker">
              <i class="fas fa-unlock-alt"></i> 解除熔断
            </button>
          </div>
        </div>
      </div>

      <!-- 当前持仓 -->
      <div class="section">
        <h3 class="section-title">
          <i class="fas fa-briefcase"></i> 当前持仓
          <span class="badge">{{ positions.length }}</span>
        </h3>
        <div v-if="positions.length === 0" class="section-empty">暂无持仓</div>
        <table v-else class="data-table">
          <thead>
            <tr>
              <th>标的</th>
              <th>名称</th>
              <th class="num">持仓量</th>
              <th class="num">成本价</th>
              <th class="num">现价</th>
              <th class="num">盈亏</th>
              <th class="num">盈亏%</th>
            </tr>
          </thead>
          <tbody>
            <tr v-for="p in positions" :key="p.id">
              <td class="symbol">{{ p.id }}</td>
              <td class="name">{{ p.name || '--' }}</td>
              <td class="num">{{ p.quantity }}</td>
              <td class="num">{{ p.price.toFixed(2) }}</td>
              <td class="num">{{ p.curPrice.toFixed(2) }}</td>
              <td class="num" :class="{ positive: (p.curPrice - p.price) > 0, negative: (p.curPrice - p.price) < 0 }">
                {{ (p.curPrice - p.price) > 0 ? '+' : '' }}{{ formatNum((p.curPrice - p.price) * p.quantity) }}
              </td>
              <td class="num" :class="{ positive: (p.curPrice / p.price - 1) > 0, negative: (p.curPrice / p.price - 1) < 0 }">
                {{ (p.curPrice / p.price - 1) > 0 ? '+' : '' }}{{ ((p.curPrice / p.price - 1) * 100).toFixed(2) }}%
              </td>
            </tr>
          </tbody>
        </table>
      </div>

      <!-- 今日交易 -->
      <div class="section">
        <h3 class="section-title">
          <i class="fas fa-exchange-alt"></i> 今日交易
          <span class="badge">{{ todayTrades.length }}</span>
        </h3>
        <div v-if="todayTrades.length === 0" class="section-empty">今日暂无交易</div>
        <table v-else class="data-table">
          <thead>
            <tr>
              <th>时间</th>
              <th>标的</th>
              <th>方向</th>
              <th class="num">成交价</th>
              <th class="num">成交量</th>
              <th class="num">成交金额</th>
              <th>状态</th>
            </tr>
          </thead>
          <tbody>
            <tr v-for="(t, i) in todayTrades" :key="i">
              <td class="time">{{ formatTradeTime(t) }}</td>
              <td class="symbol">{{ t.order?.symbol || '--' }}</td>
              <td>
                <span :class="['direction', t.order?.side === 0 ? 'buy' : 'sell']">
                  {{ t.order?.side === 0 ? '🟢 买入' : '🔴 卖出' }}
                </span>
              </td>
              <td class="num">{{ formatTradePrice(t) }}</td>
              <td class="num">{{ t.total_quantity || 0 }}</td>
              <td class="num">¥{{ formatNum(t.total_amount || 0) }}</td>
              <td><span class="status-tag">{{ tradeStatus(t) }}</span></td>
            </tr>
          </tbody>
        </table>
      </div>

      <!-- 信号日志 -->
      <div class="section">
        <h3 class="section-title">
          <i class="fas fa-terminal"></i> 信号日志
          <span class="badge">{{ signalLogs.length }}</span>
        </h3>
        <div class="log-filters">
          <input v-model="logKeyword" type="text" class="log-search" placeholder="搜索关键词..." />
          <select v-model="logLevel" class="log-level-select">
            <option value="">全部级别</option>
            <option value="INFO">INFO</option>
            <option value="WARN">WARN</option>
            <option value="ERROR">ERROR</option>
          </select>
        </div>
        <div v-if="signalLogs.length === 0" class="section-empty">暂无日志</div>
        <div v-else class="log-list">
          <div v-for="log in signalLogs" :key="log.id" class="log-item" :class="log.level?.toLowerCase()">
            <span class="log-time">{{ log.timestamp }}</span>
            <span class="log-level" :class="log.level?.toLowerCase()">{{ log.level }}</span>
            <span class="log-message">{{ log.message }}</span>
          </div>
        </div>
      </div>
    </template>
  </div>
</template>

<script setup lang="ts">
import { ref, computed, watch, onMounted, onUnmounted, inject, type Ref } from 'vue'
import axios from 'axios'

interface Position {
  id: string
  name: string
  price: number
  curPrice: number
  quantity: number
  valid_quantity: number
}

interface Trade {
  order?: { symbol: string; side: number; time: string; status: number }
  trades?: Array<{ time: string; price: number; quantity: number; amount: number; status: number }>
  total_amount?: number
  total_quantity?: number
  average_price?: number
}

interface LogEntry {
  id: number
  timestamp: string
  strategy: string
  level: string
  message: string
  context?: any
}

const serverStrategies = inject<Ref<{ name: string; running?: boolean }[]>>('serverStrategies', ref([]))
const strategies = serverStrategies

const selectedStrategy = ref('')
const loading = ref(false)
const lastRefresh = ref<Date | null>(null)

// 数据
const positions = ref<Position[]>([])
const capital = ref<{ totalAllocated: number; totalAvailable: number } | null>(null)
const todayTrades = ref<Trade[]>([])
const signalLogs = ref<LogEntry[]>([])
const logKeyword = ref('')
const logLevel = ref('')

// 风控状态
interface RiskStatus {
  breaker_level: number
  current_drawdown: number
  peak_equity: number
  breaker_tripped: boolean
  var_current: number
  var_limit: number
  var_breached: boolean
  var_ratio: number
  thresholds: { level1: number; level2: number; level3: number }
}
const riskStatus = ref<RiskStatus | null>(null)

const breakerLabel = computed(() => {
  if (!riskStatus.value) return ''
  const labels = ['🟢 正常运行', '🟡 警戒 — 禁止新开仓', '🟠 减仓 — 持仓已减半', '🔴 熔断 — 策略已停机']
  return labels[riskStatus.value.breaker_level] || labels[0]
})

const drawdownPct = computed(() => {
  if (!riskStatus.value) return 0
  return Math.min(riskStatus.value.current_drawdown * 100 / (riskStatus.value.thresholds.level3 * 100) * 100, 100)
})

const drawdownColorClass = computed(() => {
  if (!riskStatus.value) return ''
  const dd = riskStatus.value.current_drawdown
  if (dd >= riskStatus.value.thresholds.level3) return 'fill-l3'
  if (dd >= riskStatus.value.thresholds.level2) return 'fill-l2'
  if (dd >= riskStatus.value.thresholds.level1) return 'fill-l1'
  return 'fill-ok'
})

const varPct = computed(() => {
  if (!riskStatus.value || riskStatus.value.var_limit <= 0) return 0
  return Math.min(riskStatus.value.var_ratio / riskStatus.value.var_limit * 100, 100)
})

// 自动刷新
let refreshTimer: ReturnType<typeof setInterval> | null = null

const currentStrategy = computed(() =>
  strategies.value.find((s: any) => s.name === selectedStrategy.value)
)

const totalMarketValue = computed(() =>
  positions.value.reduce((sum, p) => sum + p.curPrice * p.quantity, 0)
)

const totalPnL = computed(() =>
  positions.value.reduce((sum, p) => sum + (p.curPrice - p.price) * p.quantity, 0)
)

// 加载数据
async function loadPositions() {
  try {
    const res = await axios.get('/v0/position')
    positions.value = res.data.positions || []
    if (res.data.capital) {
      capital.value = res.data.capital
    }
  } catch { positions.value = [] }
}

async function loadTrades() {
  try {
    const today = new Date()
    today.setHours(0, 0, 0, 0)
    const startTs = Math.floor(today.getTime() / 1000)

    const res = await axios.get('/v0/trade/history', {
      params: {
        strategy: selectedStrategy.value,
        start: startTs,
        page_size: 100
      }
    })
    todayTrades.value = res.data.trades || []
  } catch { todayTrades.value = [] }
}

async function loadLogs() {
  try {
    const res = await axios.get('/v0/strategy/logs', {
      params: {
        type: 'default',
        strategy: selectedStrategy.value,
        level: logLevel.value || undefined,
        keyword: logKeyword.value || undefined,
        limit: 200
      }
    })
    signalLogs.value = res.data.logs || []
  } catch { signalLogs.value = [] }
}

async function loadRiskStatus() {
  try {
    const res = await axios.get('/v0/risk/status', {
      params: { strategy: selectedStrategy.value }
    })
    riskStatus.value = res.data
  } catch { riskStatus.value = null }
}

async function resetBreaker() {
  try {
    await axios.post('/v0/risk/reset-breaker', {}, {
      params: { strategy: selectedStrategy.value }
    })
    await loadRiskStatus()
  } catch (e: any) {
    console.error('Reset breaker failed:', e)
  }
}

async function refreshAll() {
  if (!selectedStrategy.value || loading.value) return
  loading.value = true
  await Promise.all([loadPositions(), loadTrades(), loadLogs(), loadRiskStatus()])
  lastRefresh.value = new Date()
  loading.value = false
}

// 格式化
function formatNum(v: number): string {
  return Math.abs(v).toLocaleString('zh-CN', { minimumFractionDigits: 2, maximumFractionDigits: 2 })
}

function formatTime(d: Date): string {
  return d.toLocaleTimeString('zh-CN', { hour: '2-digit', minute: '2-digit', second: '2-digit' })
}

function formatTradeTime(t: Trade): string {
  const fill = t.trades?.[0]
  const raw = fill?.time || t.order?.time || ''
  if (!raw) return '--'
  // 如果是时间戳数字
  if (typeof raw === 'number') {
    return new Date(raw * 1000).toLocaleTimeString('zh-CN')
  }
  return String(raw).split(' ').pop() || raw
}

function formatTradePrice(t: Trade): string {
  if (t.average_price) return t.average_price.toFixed(2)
  const fill = t.trades?.[0]
  return fill?.price?.toFixed(2) || '--'
}

function tradeStatus(t: Trade): string {
  const status = t.order?.status ?? t.trades?.[0]?.status
  if (status == null) return '未知'
  const map: Record<number, string> = { 0: '待报', 1: '已报', 2: '部成', 3: '已成', 4: '撤单', 5: '废单' }
  return map[status] || '未知'
}

// 监听
watch(selectedStrategy, () => { refreshAll() })
watch([logKeyword, logLevel], () => { if (selectedStrategy.value) loadLogs() })

onMounted(() => {
  refreshAll()
  refreshTimer = setInterval(refreshAll, 30000) // 30秒自动刷新
})

onUnmounted(() => {
  if (refreshTimer) clearInterval(refreshTimer)
})
</script>

<style scoped>
.signal-monitor {
  display: flex;
  flex-direction: column;
  gap: 12px;
  height: 100%;
  overflow-y: auto;
  padding-bottom: 16px;
}

.monitor-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 10px 14px;
  background: rgba(15, 23, 42, 0.8);
  border: 1px solid rgba(74, 158, 255, 0.2);
  border-radius: 8px;
}
.header-left { display: flex; align-items: center; gap: 12px; }
.header-right { display: flex; align-items: center; gap: 10px; }
.strategy-select {
  padding: 6px 12px;
  background: rgba(15, 23, 42, 0.9);
  border: 1px solid rgba(74, 158, 255, 0.3);
  border-radius: 6px;
  color: #e2e8f0;
  font-size: 13px;
  min-width: 180px;
}
.strategy-status { font-size: 12px; color: #94a3b8; }
.strategy-status.running { color: #4ade80; }
.refresh-time { font-size: 11px; color: #64748b; }
.btn-refresh {
  background: rgba(96, 165, 250, 0.15);
  border: 1px solid rgba(96, 165, 250, 0.3);
  color: #60a5fa;
  padding: 4px 10px;
  border-radius: 4px;
  cursor: pointer;
  font-size: 13px;
}
.btn-refresh:hover { background: rgba(96, 165, 250, 0.3); }
.spinning { animation: spin 1s linear infinite; }
@keyframes spin { from { transform: rotate(0deg); } to { transform: rotate(360deg); } }

.section {
  background: rgba(15, 23, 42, 0.6);
  border: 1px solid rgba(74, 158, 255, 0.15);
  border-radius: 8px;
  padding: 14px;
}
.section-title {
  font-size: 13px;
  font-weight: 600;
  color: #94a3b8;
  margin: 0 0 10px 0;
  display: flex;
  align-items: center;
  gap: 8px;
}
.badge {
  background: rgba(96, 165, 250, 0.2);
  color: #60a5fa;
  font-size: 11px;
  padding: 1px 7px;
  border-radius: 10px;
}
.section-empty {
  color: #475569;
  font-size: 13px;
  text-align: center;
  padding: 20px;
}

/* 账户概览 */
.overview-grid {
  display: grid;
  grid-template-columns: repeat(4, 1fr);
  gap: 12px;
}
.overview-item { display: flex; flex-direction: column; gap: 3px; }
.overview-item .label { font-size: 11px; color: #64748b; }
.overview-item .value { font-size: 16px; font-weight: 600; color: #e2e8f0; font-family: 'SF Mono', monospace; }

/* 数据表格 */
.data-table {
  width: 100%;
  border-collapse: collapse;
  font-size: 13px;
}
.data-table th {
  text-align: left;
  padding: 7px 10px;
  color: #64748b;
  font-size: 11px;
  font-weight: 600;
  text-transform: uppercase;
  border-bottom: 1px solid rgba(74, 158, 255, 0.15);
}
.data-table td {
  padding: 7px 10px;
  border-bottom: 1px solid rgba(74, 158, 255, 0.06);
  color: #cbd5e1;
}
.data-table .num { text-align: right; font-family: 'SF Mono', monospace; font-size: 12px; }
.data-table .symbol { color: #60a5fa; font-weight: 500; }
.data-table .name { color: #94a3b8; font-size: 12px; }
.data-table .time { color: #94a3b8; font-family: 'SF Mono', monospace; font-size: 12px; }

.positive { color: #4ade80 !important; }
.negative { color: #f87171 !important; }
.direction.buy { color: #4ade80; font-weight: 500; }
.direction.sell { color: #f87171; font-weight: 500; }
.status-tag {
  font-size: 11px;
  padding: 2px 8px;
  border-radius: 4px;
  background: rgba(74, 222, 128, 0.15);
  color: #4ade80;
}

/* 日志 */
.log-filters { display: flex; gap: 8px; margin-bottom: 10px; }
.log-search {
  flex: 1;
  padding: 5px 10px;
  background: rgba(15, 23, 42, 0.8);
  border: 1px solid rgba(74, 158, 255, 0.2);
  border-radius: 4px;
  color: #e2e8f0;
  font-size: 12px;
}
.log-level-select {
  padding: 5px 8px;
  background: rgba(15, 23, 42, 0.8);
  border: 1px solid rgba(74, 158, 255, 0.2);
  border-radius: 4px;
  color: #e2e8f0;
  font-size: 12px;
}
.log-list {
  max-height: 300px;
  overflow-y: auto;
  font-family: 'SF Mono', 'Consolas', monospace;
  font-size: 12px;
}
.log-item {
  display: flex;
  gap: 8px;
  padding: 4px 6px;
  border-bottom: 1px solid rgba(74, 158, 255, 0.05);
  align-items: flex-start;
}
.log-item:hover { background: rgba(30, 41, 59, 0.5); }
.log-time { color: #475569; white-space: nowrap; min-width: 140px; }
.log-level {
  font-size: 10px;
  padding: 1px 5px;
  border-radius: 3px;
  min-width: 40px;
  text-align: center;
}
.log-level.info { background: rgba(96, 165, 250, 0.2); color: #60a5fa; }
.log-level.warn { background: rgba(251, 191, 36, 0.2); color: #fbbf24; }
.log-level.error { background: rgba(248, 113, 113, 0.2); color: #f87171; }
.log-message { color: #94a3b8; word-break: break-all; }

.empty-state {
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  padding: 60px 20px;
  color: #64748b;
  flex: 1;
}
.empty-state i { font-size: 48px; margin-bottom: 16px; opacity: 0.4; }
.empty-state p { font-size: 18px; margin: 0 0 8px; color: #94a3b8; }
.empty-state span { font-size: 13px; }

/* 风控状态区域 */
.risk-section { transition: border-color 0.3s; }
.risk-section.risk-level-1 { border-color: rgba(251, 191, 36, 0.5); }
.risk-section.risk-level-2 { border-color: rgba(251, 146, 60, 0.6); }
.risk-section.risk-level-3 { border-color: rgba(239, 68, 68, 0.7); animation: pulse-border 2s infinite; }
@keyframes pulse-border {
  0%, 100% { border-color: rgba(239, 68, 68, 0.7); }
  50% { border-color: rgba(239, 68, 68, 0.3); }
}

.breaker-badge {
  font-size: 12px;
  padding: 2px 10px;
  border-radius: 12px;
  margin-left: auto;
}
.breaker-badge.level-0 { background: rgba(74, 222, 128, 0.15); color: #4ade80; }
.breaker-badge.level-1 { background: rgba(251, 191, 36, 0.2); color: #fbbf24; }
.breaker-badge.level-2 { background: rgba(251, 146, 60, 0.2); color: #fb923c; }
.breaker-badge.level-3 { background: rgba(239, 68, 68, 0.2); color: #ef4444; }

.risk-grid {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 16px;
}
.risk-item { display: flex; flex-direction: column; gap: 6px; }
.risk-item .label { font-size: 11px; color: #64748b; }
.risk-item .value { font-size: 15px; font-weight: 600; color: #e2e8f0; font-family: 'SF Mono', monospace; }

.progress-bar {
  position: relative;
  height: 8px;
  background: rgba(30, 41, 59, 0.8);
  border-radius: 4px;
  overflow: visible;
}
.progress-fill {
  height: 100%;
  border-radius: 4px;
  transition: width 0.5s ease;
}
.progress-fill.fill-ok { background: #4ade80; }
.progress-fill.fill-l1 { background: #fbbf24; }
.progress-fill.fill-l2 { background: #fb923c; }
.progress-fill.fill-l3 { background: #ef4444; }
.progress-fill.var-fill { background: #60a5fa; }
.progress-fill.var-fill.breached { background: #ef4444; }

.threshold-markers {
  position: absolute;
  top: -2px;
  left: 0;
  right: 0;
  height: 12px;
  pointer-events: none;
}
.marker {
  position: absolute;
  width: 2px;
  height: 12px;
  transform: translateX(-1px);
}
.marker.l1 { background: #fbbf24; }
.marker.l2 { background: #fb923c; }
.marker.l3 { background: #ef4444; }

.threshold-labels {
  display: flex;
  justify-content: space-between;
  font-size: 10px;
  color: #475569;
}

.btn-reset-breaker {
  background: rgba(239, 68, 68, 0.2);
  border: 1px solid rgba(239, 68, 68, 0.4);
  color: #ef4444;
  padding: 6px 14px;
  border-radius: 6px;
  cursor: pointer;
  font-size: 12px;
  font-weight: 600;
  display: flex;
  align-items: center;
  gap: 6px;
}
.btn-reset-breaker:hover {
  background: rgba(239, 68, 68, 0.35);
}
</style>
