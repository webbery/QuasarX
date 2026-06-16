<!-- app/src/components/StrategyLogPanel.vue -->
<!-- 策略日志面板 - 筛选、统计、列表、分页、实时刷新 -->

<template>
  <div class="strategy-log-panel">
    <!-- 筛选条件 -->
    <div class="log-filters">
      <div class="filter-row">
        <label>级别:</label>
        <select v-model="filters.level">
          <option value="">全部</option>
          <option value="INFO">INFO</option>
          <option value="WARN">WARN</option>
          <option value="ERROR">ERROR</option>
        </select>

        <label style="margin-left: 16px;">时间:</label>
        <select v-model="filters.timeRange">
          <option value="1h">最近1小时</option>
          <option value="24h">今天</option>
          <option value="3d">最近3天</option>
          <option value="7d">最近7天</option>
          <option value="custom">自定义</option>
        </select>

        <!-- 自定义时间范围 -->
        <template v-if="filters.timeRange === 'custom'">
          <input type="datetime-local" v-model="filters.startTime" style="margin-left: 8px;" />
          <span style="margin: 0 4px;">至</span>
          <input type="datetime-local" v-model="filters.endTime" />
        </template>

        <label style="margin-left: 16px;">关键字:</label>
        <input type="text" v-model="filters.keyword" placeholder="搜索消息内容"
               style="padding: 4px 8px; width: 200px;" />

        <button class="btn btn-query" @click="loadLogs" style="margin-left: 16px;">
          <i class="fas fa-search"></i> 查询
        </button>

        <label style="margin-left: auto; display: flex; align-items: center; gap: 6px; cursor: pointer;">
          <input type="checkbox" v-model="autoRefresh" @change="toggleAutoRefresh" />
          <span>实时刷新 (5s)</span>
        </label>
      </div>
    </div>

    <!-- 统计概览 -->
    <div class="log-stats" v-if="stats">
      <span class="stat-item">总计: <strong>{{ stats.total_logs }}</strong></span>
      <span class="stat-item info">ℹ️ INFO: <strong>{{ stats.info_count }}</strong></span>
      <span class="stat-item warn">⚠️ WARN: <strong>{{ stats.warn_count }}</strong></span>
      <span class="stat-item error">❌ ERROR: <strong>{{ stats.error_count }}</strong></span>
    </div>

    <!-- 日志列表 -->
    <div class="log-table-wrapper">
      <div class="log-table">
        <!-- 空状态 -->
        <div v-if="logs.length === 0 && !loading" class="log-empty">
          <i class="fas fa-inbox"></i>
          <p>暂无日志</p>
        </div>

        <!-- 加载中 -->
        <div v-if="loading" class="log-loading">
          <i class="fas fa-spinner fa-spin"></i>
          <p>加载中...</p>
        </div>

        <!-- 表格 -->
        <table v-if="logs.length > 0">
          <thead>
            <tr>
              <th style="width: 180px;">时间</th>
              <th style="width: 120px;">策略</th>
              <th style="width: 70px;">级别</th>
              <th>消息</th>
              <th style="width: 60px;">详情</th>
            </tr>
          </thead>
          <tbody>
            <tr v-for="log in logs" :key="log.id" :class="{ 'log-error-row': log.level === 'ERROR' }">
              <td class="log-time">{{ log.timestamp }}</td>
              <td class="log-strategy" :title="log.strategy">
                {{ log.strategy }}
              </td>
              <td class="log-level">
                <span class="level-badge" :class="log.level.toLowerCase()">
                  {{ log.level }}
                </span>
              </td>
              <td class="log-message" @click="toggleDetail(log.id)">
                <span class="message-preview">{{ truncate(log.message, 150) }}</span>
              </td>
              <td class="log-action">
                <button class="btn-detail" @click="toggleDetail(log.id)">
                  <i :class="expandedLogId === log.id ? 'fas fa-chevron-up' : 'fas fa-chevron-down'"></i>
                </button>
              </td>
            </tr>
          </tbody>
        </table>

        <!-- 展开详情 -->
        <div v-if="expandedLog && expandedLog.context" class="log-detail-panel">
          <div class="detail-header">
            <span class="detail-time">{{ expandedLog.timestamp }}</span>
            <span class="detail-strategy">{{ expandedLog.strategy }}</span>
            <span class="level-badge" :class="expandedLog.level.toLowerCase()">{{ expandedLog.level }}</span>
            <button class="btn-close" @click="expandedLogId = null">
              <i class="fas fa-times"></i>
            </button>
          </div>
          <div class="detail-message">{{ expandedLog.message }}</div>
          <div class="detail-context">
            <strong>Context:</strong>
            <pre class="json-block">{{ formatJson(expandedLog.context) }}</pre>
          </div>
        </div>
      </div>
    </div>

    <!-- 分页 -->
    <div class="log-pagination" v-if="logs.length > 0">
      <button class="btn-page" :disabled="page <= 1" @click="page--">◀ 上一页</button>
      <span class="page-info">{{ page }} / {{ totalPages }}</span>
      <button class="btn-page" :disabled="page >= totalPages" @click="page++">下一页 ▶</button>
      <span style="margin-left: 16px; color: #64748b; font-size: 12px;">
        共 {{ totalCount }} 条
      </span>
    </div>
  </div>
</template>

<script setup>
import { ref, reactive, onMounted, onUnmounted, computed, watch } from 'vue'
import axios from 'axios'

const props = defineProps({
  strategyNames: { type: Array, default: () => [] },
  selectedStrategy: { type: String, default: '' }
})

// 筛选条件（无策略下拉，使用 props.selectedStrategy）
const filters = reactive({
  level: '',
  keyword: '',
  timeRange: '24h',
  startTime: '',
  endTime: ''
})

// 自动刷新
const autoRefresh = ref(false)
let refreshTimer = null

// 统计数据
const stats = ref(null)

// 日志列表
const logs = ref([])
const loading = ref(false)
const page = ref(1)
const pageSize = 50
const totalCount = ref(0)
const expandedLogId = ref(null)

const totalPages = computed(() => Math.max(1, Math.ceil(totalCount.value / pageSize)))
const expandedLog = computed(() => logs.value.find(l => l.id === expandedLogId.value))

// ─────────────────────────────────────────────
// 工具函数
// ─────────────────────────────────────────────

function formatDateTime(date) {
  const y = date.getFullYear()
  const m = String(date.getMonth() + 1).padStart(2, '0')
  const d = String(date.getDate()).padStart(2, '0')
  const h = String(date.getHours()).padStart(2, '0')
  const min = String(date.getMinutes()).padStart(2, '0')
  const s = String(date.getSeconds()).padStart(2, '0')
  return `${y}-${m}-${d} ${h}:${min}:${s}`
}

function getTimeRange() {
  const now = new Date()
  let start
  switch (filters.timeRange) {
    case '1h': start = new Date(now - 3600000); break
    case '24h': start = new Date(now - 86400000); break
    case '3d': start = new Date(now - 259200000); break
    case '7d': start = new Date(now - 604800000); break
    case 'custom':
      return { start: filters.startTime || '', end: filters.endTime || '' }
    default: start = new Date(now - 86400000)
  }
  return { start: formatDateTime(start), end: formatDateTime(now) }
}

function truncate(str, maxLen) {
  if (!str || str.length <= maxLen) return str
  return str.substring(0, maxLen) + '...'
}

function formatJson(str) {
  try {
    return JSON.stringify(typeof str === 'string' ? JSON.parse(str) : str, null, 2)
  } catch {
    return String(str)
  }
}

// ─────────────────────────────────────────────
// API 调用
// ─────────────────────────────────────────────

// 加载统计数据
async function loadStats() {
  try {
    const { start, end } = getTimeRange()
    const resp = await axios.get('/v0/strategy/logs', {
      params: { type: 'stats', start_time: start, end_time: end }
    })
    stats.value = resp.data
  } catch (e) {
    console.error('[StrategyLogPanel] Load stats failed:', e)
  }
}

// 加载日志
async function loadLogs() {
  loading.value = true
  try {
    const { start, end } = getTimeRange()
    const offset = (page.value - 1) * pageSize

    const params = {
      type: 'default',
      limit: pageSize,
      offset,
      start_time: start,
      end_time: end
    }

    if (props.selectedStrategy) params.strategy = props.selectedStrategy
    if (filters.level) params.level = filters.level
    if (filters.keyword) params.keyword = filters.keyword

    const resp = await axios.get('/v0/strategy/logs', { params })
    logs.value = resp.data.logs || []
    totalCount.value = resp.data.total || 0
  } catch (e) {
    console.error('[StrategyLogPanel] Load logs failed:', e)
    logs.value = []
  } finally {
    loading.value = false
  }
}

// 展开/折叠详情
function toggleDetail(id) {
  expandedLogId.value = expandedLogId.value === id ? null : id
}

// 自动刷新开关
function toggleAutoRefresh() {
  if (autoRefresh.value) {
    refreshTimer = setInterval(loadLogs, 5000)
  } else {
    if (refreshTimer) {
      clearInterval(refreshTimer)
      refreshTimer = null
    }
  }
}

// ─────────────────────────────────────────────
// 生命周期 & Watchers
// ─────────────────────────────────────────────

// 监听分页变化
watch(page, () => loadLogs())

// 监听筛选条件变化（自动重置到第1页）
watch(
  () => ({ l: filters.level, k: filters.keyword, t: filters.timeRange, s: props.selectedStrategy }),
  () => {
    page.value = 1
    loadLogs()
  },
  { deep: true }
)

onMounted(() => {
  loadStats()
  loadLogs()
})

onUnmounted(() => {
  if (refreshTimer) clearInterval(refreshTimer)
})
</script>

<style scoped>
.strategy-log-panel {
  display: flex;
  flex-direction: column;
  height: 100%;
  gap: 12px;
}

/* ── 筛选栏 ── */
.log-filters {
  background: rgba(15, 23, 42, 0.7);
  border: 1px solid rgba(74, 158, 255, 0.2);
  border-radius: 8px;
  padding: 12px;
}

.filter-row {
  display: flex;
  align-items: center;
  flex-wrap: wrap;
  gap: 8px;
}

.filter-row label {
  font-size: 13px;
  color: #94a3b8;
  font-weight: 500;
}

.filter-row select,
.filter-row input[type="datetime-local"] {
  padding: 4px 8px;
  background: rgba(15, 23, 42, 0.9);
  border: 1px solid rgba(74, 158, 255, 0.3);
  border-radius: 6px;
  color: #e2e8f0;
  font-size: 13px;
}

/* ── 统计卡片 ── */
.log-stats {
  display: flex;
  gap: 20px;
  padding: 10px 16px;
  background: rgba(15, 23, 42, 0.7);
  border: 1px solid rgba(74, 158, 255, 0.2);
  border-radius: 8px;
  font-size: 13px;
  color: #94a3b8;
}

.log-stats .stat-item strong {
  color: #e2e8f0;
  margin-left: 4px;
}

.log-stats .info strong { color: #60a5fa; }
.log-stats .warn strong { color: #fb923c; }
.log-stats .error strong { color: #f87171; }

/* ── 按钮 ── */
.btn {
  padding: 4px 12px;
  border: none;
  border-radius: 4px;
  font-size: 12px;
  cursor: pointer;
  display: inline-flex;
  align-items: center;
  gap: 4px;
  color: #e2e8f0;
  transition: all 0.15s;
}

.btn-query {
  background: rgba(96, 165, 250, 0.2);
  border: 1px solid rgba(96, 165, 250, 0.4);
}
.btn-query:hover { background: rgba(96, 165, 250, 0.35); }

/* ── 日志表格容器（填充剩余空间） ── */
.log-table-wrapper {
  flex: 1;
  display: flex;
  flex-direction: column;
  overflow: hidden;
}

.log-table {
  background: rgba(15, 23, 42, 0.5);
  border: 1px solid rgba(74, 158, 255, 0.15);
  border-radius: 8px;
  overflow: hidden;
  flex: 1;
  display: flex;
  flex-direction: column;
  min-height: 0;
}

.log-table table {
  width: 100%;
  border-collapse: collapse;
  color: #e2e8f0;
  font-size: 13px;
  flex: 1;
}

.log-table thead {
  position: sticky;
  top: 0;
  z-index: 1;
  background: rgba(15, 23, 42, 0.95);
}

.log-table tbody {
  overflow-y: auto;
}

.log-table th {
  padding: 8px 12px;
  text-align: left;
  font-weight: 600;
  color: #94a3b8;
  font-size: 12px;
  text-transform: uppercase;
  background: rgba(15, 23, 42, 0.95);
  border-bottom: 1px solid rgba(74, 158, 255, 0.2);
}

.log-table td {
  padding: 8px 12px;
  border-bottom: 1px solid rgba(74, 158, 255, 0.08);
  vertical-align: middle;
}

.log-table tbody tr {
  background: rgba(15, 23, 42, 0.5);
  transition: background 0.15s;
}

.log-table tbody tr:hover {
  background: rgba(30, 41, 59, 0.8);
}

.log-error-row {
  background: rgba(248, 113, 113, 0.05) !important;
}

/* 时间列 */
.log-time {
  font-family: 'SF Mono', 'Consolas', monospace;
  font-size: 12px;
  color: #64748b;
  white-space: nowrap;
}

/* 策略名 */
.log-strategy {
  color: #60a5fa;
  cursor: pointer;
  max-width: 120px;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}
.log-strategy:hover { text-decoration: underline; }

/* 级别标签 */
.level-badge {
  display: inline-block;
  padding: 2px 8px;
  border-radius: 10px;
  font-size: 11px;
  font-weight: 600;
}
.level-badge.info {
  background: rgba(96, 165, 250, 0.15);
  color: #60a5fa;
}
.level-badge.warn {
  background: rgba(251, 146, 60, 0.15);
  color: #fb923c;
}
.level-badge.error {
  background: rgba(248, 113, 113, 0.15);
  color: #f87171;
}

/* 消息 */
.log-message { cursor: pointer; }
.message-preview {
  color: #cbd5e1;
  word-break: break-all;
}

/* 展开按钮 */
.log-action .btn-detail {
  background: transparent;
  border: none;
  color: #64748b;
  cursor: pointer;
  padding: 2px;
  font-size: 12px;
}
.log-action .btn-detail:hover { color: #60a5fa; }

/* 空状态 & 加载状态 */
.log-empty, .log-loading {
  display: flex;
  flex-direction: column;
  align-items: center;
  padding: 40px;
  color: #64748b;
}
.log-empty i, .log-loading i {
  font-size: 32px;
  margin-bottom: 12px;
  opacity: 0.4;
}
.log-loading i { opacity: 0.8; }

/* ── 展开详情 ── */
.log-detail-panel {
  margin: 8px 0;
  padding: 12px 16px;
  background: rgba(30, 41, 59, 0.9);
  border: 1px solid rgba(74, 158, 255, 0.2);
  border-radius: 8px;
}

.detail-header {
  display: flex;
  align-items: center;
  gap: 12px;
  margin-bottom: 8px;
  padding-bottom: 8px;
  border-bottom: 1px solid rgba(74, 158, 255, 0.1);
}

.detail-time {
  font-family: 'SF Mono', 'Consolas', monospace;
  color: #64748b;
  font-size: 12px;
}

.detail-strategy {
  color: #60a5fa;
  font-weight: 600;
}

.btn-close {
  margin-left: auto;
  background: transparent;
  border: none;
  color: #64748b;
  cursor: pointer;
  font-size: 14px;
}
.btn-close:hover { color: #f87171; }

.detail-message {
  color: #e2e8f0;
  margin-bottom: 12px;
  line-height: 1.6;
  white-space: pre-wrap;
}

.detail-context strong {
  color: #94a3b8;
  font-size: 12px;
  text-transform: uppercase;
}

.json-block {
  margin-top: 8px;
  padding: 10px;
  background: rgba(15, 23, 42, 0.8);
  border-radius: 6px;
  font-size: 12px;
  font-family: 'SF Mono', 'Consolas', monospace;
  color: #a5b4fc;
  overflow-x: auto;
  max-height: 300px;
  overflow-y: auto;
}

/* ── 分页 ── */
.log-pagination {
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 12px;
  padding: 12px 0;
  color: #94a3b8;
  font-size: 13px;
}

.btn-page {
  padding: 4px 12px;
  background: rgba(96, 165, 250, 0.1);
  border: 1px solid rgba(96, 165, 250, 0.3);
  border-radius: 4px;
  color: #60a5fa;
  font-size: 12px;
  cursor: pointer;
}
.btn-page:disabled {
  opacity: 0.3;
  cursor: not-allowed;
}
.btn-page:hover:not(:disabled) {
  background: rgba(96, 165, 250, 0.25);
}

.page-info {
  font-family: 'SF Mono', 'Consolas', monospace;
}
</style>
