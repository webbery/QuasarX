<!-- app/src/components/StrategySelectPanel.vue -->
<!-- 统一日志面板 - 支持普通日志和节点日志切换 -->

<template>
  <div class="strategy-select-panel">
    <!-- 日志类型切换 + 统计信息 -->
    <div class="log-type-selector">
      <label>日志类型:</label>
      <select v-model="logType" @change="onLogTypeChange">
        <option value="normal">普通日志</option>
        <option value="node">节点日志</option>
      </select>

      <!-- 普通日志统计（靠右） -->
      <div v-if="logType === 'normal' && normalStats" class="log-stats-inline">
        <span class="stat-item">总计: <strong>{{ normalStats.total_logs }}</strong></span>
        <span class="stat-item info">ℹ️ <strong>{{ normalStats.info_count }}</strong></span>
        <span class="stat-item important">🔥 <strong>{{ normalStats.important_count }}</strong></span>
        <span class="stat-item warn">⚠️ <strong>{{ normalStats.warn_count }}</strong></span>
        <span class="stat-item error">❌ <strong>{{ normalStats.error_count }}</strong></span>
      </div>

      <!-- 节点日志统计（靠右） -->
      <div v-else-if="logType === 'node' && nodeStats" class="log-stats-inline">
        <span class="stat-item">总计: <strong>{{ nodeStats.total }}</strong></span>
        <span class="stat-item input-type">Input: <strong>{{ nodeStats.input_count }}</strong></span>
        <span class="stat-item signal-type">Signal: <strong>{{ nodeStats.signal_count }}</strong></span>
        <span class="stat-item portfolio-type">Portfolio: <strong>{{ nodeStats.portfolio_count }}</strong></span>
        <span class="stat-item execution-type">Execution: <strong>{{ nodeStats.execution_count }}</strong></span>
      </div>
    </div>

    <!-- 普通日志筛选条件 -->
    <div v-if="logType === 'normal'" class="log-filters">
      <div class="filter-row">
        <label>级别:</label>
        <select v-model="normalFilters.level">
          <option value="">全部</option>
          <option value="INFO">INFO</option>
          <option value="IMPORTANT">🔥 IMPORTANT</option>
          <option value="WARN">WARN</option>
          <option value="ERROR">ERROR</option>
        </select>

        <label style="margin-left: 16px;">时间:</label>
        <select v-model="normalFilters.timeRange">
          <option value="1h">最近1小时</option>
          <option value="24h">今天</option>
          <option value="3d">最近3天</option>
          <option value="7d">最近7天</option>
          <option value="custom">自定义</option>
        </select>

        <template v-if="normalFilters.timeRange === 'custom'">
          <input type="datetime-local" v-model="normalFilters.startTime" style="margin-left: 8px;" />
          <span style="margin: 0 4px;">至</span>
          <input type="datetime-local" v-model="normalFilters.endTime" />
        </template>

        <label style="margin-left: 16px;">关键字:</label>
        <input type="text" v-model="normalFilters.keyword" placeholder="搜索消息内容"
               style="padding: 4px 8px; width: 200px;" />

        <button class="btn btn-query" @click="loadNormalLogs" style="margin-left: 16px;">
          <i class="fas fa-search"></i> 查询
        </button>

        <button class="btn btn-delete" @click="handleDeleteNormal" style="margin-left: 8px;">
          <i class="fas fa-trash"></i> 删除日志
        </button>

        <label style="margin-left: auto; display: flex; align-items: center; gap: 6px; cursor: pointer;">
          <input type="checkbox" v-model="autoRefresh" @change="toggleAutoRefresh" />
          <span>实时刷新 (5s)</span>
        </label>
      </div>
    </div>

    <!-- 节点日志筛选条件 -->
    <div v-else class="log-filters">
      <div class="filter-row">
        <label>节点类型:</label>
        <select v-model="nodeFilters.nodeType">
          <option value="">全部</option>
          <option value="input">Input</option>
          <option value="signal">Signal</option>
          <option value="portfolio">Portfolio</option>
          <option value="execution">Execution</option>
        </select>

        <label style="margin-left: 16px;">Epoch 范围:</label>
        <input type="number" v-model.number="nodeFilters.epochFrom" placeholder="起始"
               style="width: 80px; padding: 4px 8px;" />
        <span style="color: #64748b;">~</span>
        <input type="number" v-model.number="nodeFilters.epochTo" placeholder="结束"
               style="width: 80px; padding: 4px 8px;" />

        <label style="margin-left: 16px;">时间:</label>
        <select v-model="nodeFilters.timeRange">
          <option value="1h">最近1小时</option>
          <option value="24h">今天</option>
          <option value="3d">最近3天</option>
          <option value="7d">最近7天</option>
          <option value="custom">自定义</option>
        </select>

        <template v-if="nodeFilters.timeRange === 'custom'">
          <input type="datetime-local" v-model="nodeFilters.startTime" style="margin-left: 8px;" />
          <span style="margin: 0 4px;">至</span>
          <input type="datetime-local" v-model="nodeFilters.endTime" />
        </template>

        <button class="btn btn-query" @click="loadNodeLogs" style="margin-left: 16px;">
          <i class="fas fa-search"></i> 查询
        </button>

        <label style="margin-left: auto; display: flex; align-items: center; gap: 6px; cursor: pointer;">
          <input type="checkbox" v-model="autoRefresh" @change="toggleAutoRefresh" />
          <span>实时刷新 (5s)</span>
        </label>
      </div>
    </div>

    <!-- 日志列表 -->
    <div class="log-table-wrapper">
      <div class="log-table">
        <!-- 空状态 -->
        <div v-if="currentLogs.length === 0 && !loading" class="log-empty">
          <i class="fas fa-inbox"></i>
          <p>暂无日志</p>
        </div>

        <!-- 加载中 -->
        <div v-if="loading" class="log-loading">
          <i class="fas fa-spinner fa-spin"></i>
          <p>加载中...</p>
        </div>

        <!-- 普通日志表格 -->
        <table v-if="logType === 'normal' && currentLogs.length > 0">
          <colgroup>
            <col style="width: 180px;" />
            <col style="width: 120px;" />
            <col style="width: 70px;" />
            <col />
            <col style="width: 60px;" />
          </colgroup>
          <thead>
            <tr>
              <th class="sortable" @click="toggleSortOrder">
                时间
                <i v-if="sortState === 'asc'" class="fas fa-sort-up"></i>
                <i v-else-if="sortState === 'desc'" class="fas fa-sort-down"></i>
                <i v-else class="fas fa-sort"></i>
              </th>
              <th>策略</th>
              <th>级别</th>
              <th>消息</th>
              <th>详情</th>
            </tr>
          </thead>
          <tbody>
            <tr v-for="log in currentLogs" :key="log.id" :class="{ 'log-error-row': log.level === 'ERROR', 'log-important-row': log.level === 'IMPORTANT' }">
              <td class="log-time">{{ log.timestamp }}</td>
              <td class="log-strategy" :title="log.strategy">{{ log.strategy }}</td>
              <td class="log-level">
                <span class="level-badge" :class="log.level.toLowerCase()">{{ log.level }}</span>
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

        <!-- 节点日志表格 -->
        <table v-else-if="logType === 'node' && currentLogs.length > 0">
          <colgroup>
            <col style="width: 80px;" />
            <col style="width: 100px;" />
            <col style="width: 80px;" />
            <col />
            <col />
            <col style="width: 150px;" />
            <col style="width: 60px;" />
          </colgroup>
          <thead>
            <tr>
              <th>Epoch</th>
              <th>节点类型</th>
              <th>节点 ID</th>
              <th>输入摘要</th>
              <th>输出摘要</th>
              <th class="sortable" @click="toggleSortOrder">
                时间
                <i v-if="sortState === 'asc'" class="fas fa-sort-up"></i>
                <i v-else-if="sortState === 'desc'" class="fas fa-sort-down"></i>
                <i v-else class="fas fa-sort"></i>
              </th>
              <th>详情</th>
            </tr>
          </thead>
          <tbody>
            <tr v-for="log in currentLogs" :key="log.id">
              <td class="log-epoch">{{ log.epoch }}</td>
              <td class="log-node-type">
                <span class="type-badge" :class="log.node_type">{{ log.node_type }}</span>
              </td>
              <td class="log-node-id">{{ log.node_id }}</td>
              <td class="log-summary" @click="toggleDetail(log.id)">
                <span class="summary-preview">{{ truncate(log.input_summary, 120) }}</span>
              </td>
              <td class="log-summary" @click="toggleDetail(log.id)">
                <span class="summary-preview">{{ truncate(log.output_summary, 120) }}</span>
              </td>
              <td class="log-time">{{ log.timestamp }}</td>
              <td class="log-action">
                <button class="btn-detail" @click="toggleDetail(log.id)">
                  <i :class="expandedLogId === log.id ? 'fas fa-chevron-up' : 'fas fa-chevron-down'"></i>
                </button>
              </td>
            </tr>
          </tbody>
        </table>

        <!-- 普通日志展开详情 -->
        <div v-if="logType === 'normal' && expandedLog && expandedLog.context" class="log-detail-panel">
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

        <!-- 节点日志展开详情 -->
        <div v-else-if="logType === 'node' && expandedLog" class="log-detail-panel">
          <div class="detail-header">
            <span class="detail-epoch">Epoch {{ expandedLog.epoch }}</span>
            <span class="type-badge" :class="expandedLog.node_type">{{ expandedLog.node_type }}</span>
            <span class="detail-node-id">Node {{ expandedLog.node_id }}</span>
            <span class="detail-time">{{ expandedLog.timestamp }}</span>
            <button class="btn-close" @click="expandedLogId = null">
              <i class="fas fa-times"></i>
            </button>
          </div>
          <div class="detail-sections">
            <div class="detail-section">
              <strong>Input:</strong>
              <pre class="json-block">{{ formatJson(expandedLog.input) }}</pre>
            </div>
            <div class="detail-section">
              <strong>Output:</strong>
              <pre class="json-block">{{ formatJson(expandedLog.output) }}</pre>
            </div>
          </div>
        </div>
      </div>
    </div>

    <!-- 分页 -->
    <div class="log-pagination" v-if="currentLogs.length > 0">
      <button class="btn-page" :disabled="page <= 1" @click="page--">◀ 上一页</button>
      <span class="page-info">{{ page }} / {{ totalPages }}</span>
      <button class="btn-page" :disabled="page >= totalPages" @click="page++">下一页 ▶</button>
      <span style="margin-left: 16px; color: #64748b; font-size: 12px;">
        共 {{ totalCount }} 条
      </span>
    </div>

    <!-- 确认对话框 -->
    <PromptDialog ref="promptDialogRef" />
  </div>
</template>

<script setup>
import { ref, reactive, onMounted, onUnmounted, computed, watch } from 'vue'
import axios from 'axios'
import { message } from '../tool'
import PromptDialog from './PromptDialog.vue'

const props = defineProps({
  strategyNames: { type: Array, default: () => [] },
  selectedStrategy: { type: String, default: '' }
})

// PromptDialog 引用
const promptDialogRef = ref(null)

// 日志类型
const logType = ref('normal')

// 自动刷新
const autoRefresh = ref(false)
let refreshTimer = null

// 展开的日志 ID
const expandedLogId = ref(null)

// 分页
const page = ref(1)
const pageSize = 50
const totalCount = ref(0)

// 排序状态 ('' = 无排序, 'asc' = 升序, 'desc' = 降序)
const sortState = ref('')

const totalPages = computed(() => Math.max(1, Math.ceil(totalCount.value / pageSize)))

// ─────────────────────────────────────────────
// 普通日志状态
// ─────────────────────────────────────────────

const normalFilters = reactive({
  level: '',
  keyword: '',
  timeRange: '24h',
  startTime: '',
  endTime: ''
})

const normalLogs = ref([])
const normalStats = ref(null)

// ─────────────────────────────────────────────
// 节点日志状态
// ─────────────────────────────────────────────

const nodeFilters = reactive({
  nodeType: '',
  epochFrom: null,
  epochTo: null,
  timeRange: '24h',
  startTime: '',
  endTime: ''
})

const nodeLogs = ref([])
const nodeStats = ref(null)

// ─────────────────────────────────────────────
// 计算属性
// ─────────────────────────────────────────────

const loading = ref(false)
const currentLogs = computed(() => logType.value === 'normal' ? normalLogs.value : nodeLogs.value)
const expandedLog = computed(() => currentLogs.value.find(l => l.id === expandedLogId.value))

// ─────────────────────────────────────────────
// 工具函数
// ─────────────────────────────────────────────

// 排序切换
function toggleSortOrder() {
  if (sortState.value === '') {
    sortState.value = 'asc'
  } else if (sortState.value === 'asc') {
    sortState.value = 'desc'
  } else {
    sortState.value = ''
  }
  // 重新加载当前日志
  if (logType.value === 'normal') {
    loadNormalLogs()
  } else {
    loadNodeLogs()
  }
}

function formatDateTime(date) {
  const y = date.getFullYear()
  const m = String(date.getMonth() + 1).padStart(2, '0')
  const d = String(date.getDate()).padStart(2, '0')
  const h = String(date.getHours()).padStart(2, '0')
  const min = String(date.getMinutes()).padStart(2, '0')
  const s = String(date.getSeconds()).padStart(2, '0')
  return `${y}-${m}-${d} ${h}:${min}:${s}`
}

function getNormalTimeRange() {
  const now = new Date()
  let start
  switch (normalFilters.timeRange) {
    case '1h': start = new Date(now - 3600000); break
    case '24h': start = new Date(now - 86400000); break
    case '3d': start = new Date(now - 259200000); break
    case '7d': start = new Date(now - 604800000); break
    case 'custom':
      return { start: normalFilters.startTime || '', end: normalFilters.endTime || '' }
    default: start = new Date(now - 86400000)
  }
  return { start: formatDateTime(start), end: formatDateTime(now) }
}

function getNodeTimeRange() {
  const now = new Date()
  let start
  switch (nodeFilters.timeRange) {
    case '1h': start = new Date(now - 3600000); break
    case '24h': start = new Date(now - 86400000); break
    case '3d': start = new Date(now - 259200000); break
    case '7d': start = new Date(now - 604800000); break
    case 'custom':
      return { start: nodeFilters.startTime || '', end: nodeFilters.endTime || '' }
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
// 日志类型切换
// ─────────────────────────────────────────────

function onLogTypeChange() {
  page.value = 1
  expandedLogId.value = null
  sortState.value = ''  // 重置排序
  if (logType.value === 'normal') {
    loadNormalLogs()
  } else {
    loadNodeLogs()
  }
}

// ─────────────────────────────────────────────
// 普通日志 API 调用
// ─────────────────────────────────────────────

async function loadNormalStats() {
  try {
    const { start, end } = getNormalTimeRange()
    const resp = await axios.get('/v0/strategy/logs', {
      params: { type: 'stats', start_time: start, end_time: end }
    })
    normalStats.value = resp.data
  } catch (e) {
    console.error('[StrategySelectPanel] Load normal stats failed:', e)
  }
}

async function loadNormalLogs() {
  loading.value = true
  try {
    const { start, end } = getNormalTimeRange()
    const offset = (page.value - 1) * pageSize

    const params = {
      type: 'default',
      limit: pageSize,
      offset,
      start_time: start,
      end_time: end
    }

    if (props.selectedStrategy) params.strategy = props.selectedStrategy
    if (normalFilters.level) params.level = normalFilters.level
    if (normalFilters.keyword) params.keyword = normalFilters.keyword

    const resp = await axios.get('/v0/strategy/logs', { params })
    let logs = resp.data.logs || []
    
    // 客户端排序
    if (sortState.value) {
      logs.sort((a, b) => {
        const timeA = new Date(a.timestamp).getTime()
        const timeB = new Date(b.timestamp).getTime()
        return sortState.value === 'asc' ? timeA - timeB : timeB - timeA
      })
    }
    
    normalLogs.value = logs
    totalCount.value = resp.data.total || 0
  } catch (e) {
    console.error('[StrategySelectPanel] Load normal logs failed:', e)
    normalLogs.value = []
  } finally {
    loading.value = false
  }
}

async function handleDeleteNormal() {
  const conditions = []
  if (props.selectedStrategy) conditions.push(`策略: ${props.selectedStrategy}`)
  if (normalFilters.level) conditions.push(`级别: ${normalFilters.level}`)
  if (normalFilters.keyword) conditions.push(`关键字: ${normalFilters.keyword}`)
  const { start, end } = getNormalTimeRange()
  if (start || end) conditions.push(`时间: ${start || '不限'} ~ ${end || '不限'}`)

  if (conditions.length === 0) {
    message.warning('请至少设置一个筛选条件')
    return
  }

  const confirmed = await promptDialogRef.value.confirm({
    title: '删除日志',
    message: `确定要删除以下日志吗？此操作不可恢复。\n\n删除条件：\n${conditions.join('\n')}`
  })

  if (!confirmed) return

  try {
    loading.value = true
    const resp = await axios.delete('/v0/strategy/logs', {
      data: {
        strategy: props.selectedStrategy || '',
        level: normalFilters.level || '',
        start_time: start,
        end_time: end
      }
    })
    message.success(`已删除 ${resp.data.deleted_count} 条日志`)
    await loadNormalStats()
    await loadNormalLogs()
  } catch (e) {
    message.error('删除失败: ' + (e.response?.data?.error || e.message))
  } finally {
    loading.value = false
  }
}

// ─────────────────────────────────────────────
// 节点日志 API 调用
// ─────────────────────────────────────────────

async function loadNodeStats() {
  try {
    const { start, end } = getNodeTimeRange()
    const params = { type: 'stats', start_time: start, end_time: end }
    if (props.selectedStrategy) params.strategy = props.selectedStrategy
    if (nodeFilters.nodeType) params.node_type = nodeFilters.nodeType

    const resp = await axios.get('/v0/node/io', { params })
    nodeStats.value = resp.data
  } catch (e) {
    console.error('[StrategySelectPanel] Load node stats failed:', e)
  }
}

async function loadNodeLogs() {
  loading.value = true
  try {
    const { start, end } = getNodeTimeRange()
    const offset = (page.value - 1) * pageSize

    const params = {
      limit: pageSize,
      offset,
      start_time: start,
      end_time: end
    }

    if (props.selectedStrategy) params.strategy = props.selectedStrategy
    if (nodeFilters.nodeType) params.node_type = nodeFilters.nodeType
    if (nodeFilters.epochFrom != null) params.epoch_from = nodeFilters.epochFrom
    if (nodeFilters.epochTo != null) params.epoch_to = nodeFilters.epochTo

    const resp = await axios.get('/v0/node/io', { params })
    let logs = resp.data.logs || []
    
    // 客户端排序
    if (sortState.value) {
      logs.sort((a, b) => {
        const timeA = new Date(a.timestamp).getTime()
        const timeB = new Date(b.timestamp).getTime()
        return sortState.value === 'asc' ? timeA - timeB : timeB - timeA
      })
    }
    
    nodeLogs.value = logs
    totalCount.value = resp.data.total || 0
  } catch (e) {
    console.error('[StrategySelectPanel] Load node logs failed:', e)
    nodeLogs.value = []
  } finally {
    loading.value = false
  }
}

// ─────────────────────────────────────────────
// 展开/折叠详情
// ─────────────────────────────────────────────

function toggleDetail(id) {
  expandedLogId.value = expandedLogId.value === id ? null : id
}

// ─────────────────────────────────────────────
// 自动刷新
// ─────────────────────────────────────────────

function toggleAutoRefresh() {
  if (autoRefresh.value) {
    refreshTimer = setInterval(() => {
      if (logType.value === 'normal') {
        loadNormalLogs()
      } else {
        loadNodeLogs()
      }
    }, 5000)
  } else {
    if (refreshTimer) {
      clearInterval(refreshTimer)
      refreshTimer = null
    }
  }
}

// ─────────────────────────────────────────────
// Watchers
// ─────────────────────────────────────────────

watch(page, () => {
  if (logType.value === 'normal') {
    loadNormalLogs()
  } else {
    loadNodeLogs()
  }
})

watch(
  () => ({ l: normalFilters.level, k: normalFilters.keyword, t: normalFilters.timeRange, s: props.selectedStrategy }),
  () => {
    if (logType.value === 'normal') {
      page.value = 1
      loadNormalLogs()
    }
  },
  { deep: true }
)

watch(
  () => ({
    nt: nodeFilters.nodeType,
    ef: nodeFilters.epochFrom,
    et: nodeFilters.epochTo,
    t: nodeFilters.timeRange,
    s: props.selectedStrategy
  }),
  () => {
    if (logType.value === 'node') {
      page.value = 1
      loadNodeLogs()
    }
  },
  { deep: true }
)

// ─────────────────────────────────────────────
// 生命周期
// ─────────────────────────────────────────────

onMounted(() => {
  loadNormalStats()
  loadNormalLogs()
})

onUnmounted(() => {
  if (refreshTimer) clearInterval(refreshTimer)
})
</script>

<style scoped>
/* ── 统一滚动条（thin 风格） ── */
.log-table-wrapper::-webkit-scrollbar,
.log-table::-webkit-scrollbar,
.json-block::-webkit-scrollbar {
  width: 6px;
  height: 6px;
}

.log-table-wrapper::-webkit-scrollbar-track,
.log-table::-webkit-scrollbar-track,
.json-block::-webkit-scrollbar-track {
  background: transparent;
}

.log-table-wrapper::-webkit-scrollbar-thumb,
.log-table::-webkit-scrollbar-thumb,
.json-block::-webkit-scrollbar-thumb {
  background: rgba(100, 116, 139, 0.4);
  border-radius: 3px;
}

.log-table-wrapper::-webkit-scrollbar-thumb:hover,
.log-table::-webkit-scrollbar-thumb:hover,
.json-block::-webkit-scrollbar-thumb:hover {
  background: rgba(100, 116, 139, 0.6);
}

/* 表格 tbody 滚动 */
.log-table tbody {
  overflow-y: auto;
}

.strategy-select-panel {
  display: flex;
  flex-direction: column;
  height: 100%;
  gap: 12px;
}

/* ── 日志类型选择器 ── */
.log-type-selector {
  display: flex;
  align-items: center;
  gap: 12px;
  padding: 8px 12px;
  background: rgba(15, 23, 42, 0.7);
  border: 1px solid rgba(74, 158, 255, 0.2);
  border-radius: 8px;
}

.log-type-selector label {
  font-size: 13px;
  color: #94a3b8;
  font-weight: 500;
}

.log-type-selector select {
  padding: 4px 8px;
  background: rgba(15, 23, 42, 0.9);
  border: 1px solid rgba(74, 158, 255, 0.3);
  border-radius: 6px;
  color: #e2e8f0;
  font-size: 13px;
  cursor: pointer;
}

/* ── 统计信息（行内靠右） ── */
.log-stats-inline {
  display: flex;
  align-items: center;
  gap: 16px;
  margin-left: auto;
  font-size: 12px;
  color: #94a3b8;
}

.log-stats-inline .stat-item strong {
  color: #e2e8f0;
  margin-left: 2px;
}

.log-stats-inline .info strong { color: #60a5fa; }
.log-stats-inline .warn strong { color: #fb923c; }
.log-stats-inline .error strong { color: #f87171; }
.log-stats-inline .input-type strong { color: #60a5fa; }
.log-stats-inline .signal-type strong { color: #fbbf24; }
.log-stats-inline .portfolio-type strong { color: #a78bfa; }
.log-stats-inline .execution-type strong { color: #34d399; }

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
.filter-row input[type="datetime-local"],
.filter-row input[type="number"] {
  padding: 4px 8px;
  background: rgba(15, 23, 42, 0.9);
  border: 1px solid rgba(74, 158, 255, 0.3);
  border-radius: 6px;
  color: #e2e8f0;
  font-size: 13px;
}

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

.btn-delete {
  background: rgba(248, 113, 113, 0.15);
  border: 1px solid rgba(248, 113, 113, 0.4);
  color: #f87171;
}
.btn-delete:hover { background: rgba(248, 113, 113, 0.35); }

/* ── 日志表格容器 ── */
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
  table-layout: fixed;
}

.log-table thead {
  position: sticky;
  top: 0;
  z-index: 1;
  background: rgba(15, 23, 42, 0.95);
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

/* 可排序列 */
.log-table th.sortable {
  cursor: pointer;
  user-select: none;
}

.log-table th.sortable:hover {
  color: #e2e8f0;
}

.log-table th.sortable i {
  margin-left: 4px;
  opacity: 0.4;
  font-size: 11px;
}

.log-table th.sortable:hover i {
  opacity: 0.7;
}

.log-table th.sortable .fa-sort-up,
.log-table th.sortable .fa-sort-down {
  opacity: 1 !important;
  color: #60a5fa;
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

/* Epoch 列 */
.log-epoch {
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
.level-badge.important {
  background: rgba(251, 191, 36, 0.2);
  color: #fbbf24;
  font-weight: 700;
  border: 1px solid rgba(251, 191, 36, 0.4);
}

/* IMPORTANT 日志行背景 */
.log-important-row {
  background: rgba(251, 191, 36, 0.08) !important;
}
.log-important-row:hover {
  background: rgba(251, 191, 36, 0.15) !important;
}

/* 节点类型标签 */
.type-badge {
  display: inline-block;
  padding: 2px 8px;
  border-radius: 10px;
  font-size: 11px;
  font-weight: 600;
  text-transform: capitalize;
}
.type-badge.input {
  background: rgba(96, 165, 250, 0.15);
  color: #60a5fa;
}
.type-badge.signal {
  background: rgba(251, 191, 36, 0.15);
  color: #fbbf24;
}
.type-badge.portfolio {
  background: rgba(167, 139, 250, 0.15);
  color: #a78bfa;
}
.type-badge.execution {
  background: rgba(52, 211, 153, 0.15);
  color: #34d399;
}

/* 节点 ID */
.log-node-id {
  font-family: 'SF Mono', 'Consolas', monospace;
  font-size: 12px;
  color: #cbd5e1;
}

/* 消息 */
.log-message { cursor: pointer; }
.message-preview {
  color: #cbd5e1;
  word-break: break-all;
}

/* 摘要 */
.log-summary { cursor: pointer; }
.summary-preview {
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

.detail-epoch {
  font-family: 'SF Mono', 'Consolas', monospace;
  color: #64748b;
  font-size: 12px;
}

.detail-node-id {
  font-family: 'SF Mono', 'Consolas', monospace;
  color: #cbd5e1;
  font-size: 12px;
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

.detail-sections {
  display: flex;
  flex-direction: column;
  gap: 12px;
}

.detail-section strong {
  color: #94a3b8;
  font-size: 12px;
  text-transform: uppercase;
  display: block;
  margin-bottom: 4px;
}

.json-block {
  margin: 0;
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
