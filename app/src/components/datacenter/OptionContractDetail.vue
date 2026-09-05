<template>
  <div class="option-detail-root">
    <div class="detail-header">
      <div class="header-info">
        <div class="contract-code">{{ contractName || contractCode || '—' }}</div>
        <div class="contract-meta">
          <span v-if="exchange" class="meta-badge">{{ exchange }}</span>
          <span v-if="product" class="meta-badge">{{ product }}</span>
          <span v-if="callPut" class="meta-badge" :class="callPut === 'call' ? 'badge-call' : 'badge-put'">
            {{ callPut === 'call' ? '认购' : '认沽' }}
          </span>
          <span v-if="strikePrice" class="meta-badge">行权 {{ strikePrice }}</span>
        </div>
      </div>
      <div class="header-actions">
        <button class="btn-refresh" @click="fetchData" :disabled="loading" title="刷新数据">
          <i class="fas fa-sync-alt" :class="{ 'fa-spin': loading }"></i>
        </button>
      </div>
    </div>

    <div class="detail-body">
      <div v-if="!symbolId" class="empty-tip">
        <i class="fas fa-mouse-pointer"></i>
        <p>从左侧合约列表点击一行查看详情</p>
      </div>

      <div v-else-if="loading" class="loading-tip">
        <i class="fas fa-spinner fa-spin"></i> 加载中…
      </div>

      <div v-else-if="error" class="error-tip">
        <i class="fas fa-exclamation-triangle"></i> {{ error }}
      </div>

      <template v-else>
        <div class="summary-row">
          <div class="summary-card">
            <div class="summary-label">数据天数</div>
            <div class="summary-value">{{ rows.length }}</div>
          </div>
          <div class="summary-card">
            <div class="summary-label">日期范围</div>
            <div class="summary-value small">
              {{ rows.length ? `${rows[0].trade_date} ~ ${rows[rows.length - 1].trade_date}` : '—' }}
            </div>
          </div>
          <div class="summary-card">
            <div class="summary-label">累计成交量</div>
            <div class="summary-value">{{ formatVolume(totalVolume) }}</div>
          </div>
        </div>

        <div v-if="rows.length === 0" class="empty-tip small">
          <i class="fas fa-info-circle"></i> 该合约暂无日线数据
        </div>

        <table v-else class="detail-table">
          <thead>
            <tr>
              <th>日期</th>
              <th class="num">开盘</th>
              <th class="num">最高</th>
              <th class="num">最低</th>
              <th class="num">收盘</th>
              <th class="num">结算价</th>
              <th class="num">成交量</th>
              <th class="num">持仓量</th>
            </tr>
          </thead>
          <tbody>
            <tr v-for="(r, i) in pagedRows" :key="i">
              <td>{{ r.trade_date }}</td>
              <td class="num">{{ fmt(r.open) }}</td>
              <td class="num">{{ fmt(r.high) }}</td>
              <td class="num">{{ fmt(r.low) }}</td>
              <td class="num">{{ fmt(r.close) }}</td>
              <td class="num">{{ fmt(r.settlement) }}</td>
              <td class="num">{{ r.volume?.toLocaleString() || '—' }}</td>
              <td class="num">{{ r.open_interest?.toLocaleString() || '—' }}</td>
            </tr>
          </tbody>
        </table>

        <div class="pagination-center" v-if="rows.length > pageSize">
          <button class="page-btn" :disabled="page === 1" @click="page = 1">
            <i class="fas fa-angle-double-left"></i>
          </button>
          <button class="page-btn" :disabled="page === 1" @click="page--">
            <i class="fas fa-angle-left"></i>
          </button>
          <span class="page-info">第 {{ page }} / {{ totalPages }} 页</span>
          <button class="page-btn" :disabled="page === totalPages" @click="page++">
            <i class="fas fa-angle-right"></i>
          </button>
          <button class="page-btn" :disabled="page === totalPages" @click="page = totalPages">
            <i class="fas fa-angle-double-right"></i>
          </button>
        </div>
      </template>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, computed, watch } from 'vue'
import axios from 'axios'

const props = defineProps<{
  contractCode: string | null
  contractName?: string
  exchange?: string
  product?: string
  symbolId?: number | null
  callPut?: string
  strikePrice?: number | null
}>()

const loading = ref(false)
const error = ref('')
const rows = ref<any[]>([])
const page = ref(1)
const pageSize = 100

const contractCode = computed(() => props.contractCode)
const contractName = computed(() => props.contractName || '')
const exchange = computed(() => props.exchange || '')
const product = computed(() => props.product || '')
const callPut = computed(() => props.callPut || '')
const strikePrice = computed(() => props.strikePrice)

const totalPages = computed(() => Math.max(1, Math.ceil(rows.value.length / pageSize)))
const pagedRows = computed(() => {
  const start = (page.value - 1) * pageSize
  return rows.value.slice(start, start + pageSize)
})
const totalVolume = computed(() => rows.value.reduce((s, r) => s + (r.volume || 0), 0))

function fmt(v: any): string {
  if (v == null || v === 0) return '—'
  return Number(v).toFixed(4)
}

function formatVolume(v: number): string {
  if (v >= 1e8) return (v / 1e8).toFixed(2) + '亿'
  if (v >= 1e4) return (v / 1e4).toFixed(2) + '万'
  return v.toLocaleString()
}

async function fetchData() {
  if (!props.symbolId) return
  loading.value = true
  error.value = ''
  page.value = 1
  const server = localStorage.getItem('remote')
  const token = localStorage.getItem('token')
  try {
    const resp = await axios.get(`https://${server}/v0/option/data`, {
      params: { symbol_id: props.symbolId, limit: 10000 },
      headers: { 'Authorization': token || '' }
    })
    rows.value = resp.data.data || []
    if (resp.data.error) {
      error.value = resp.data.error
    }
  } catch (err: any) {
    error.value = err.response?.data?.message || err.message
    rows.value = []
  } finally {
    loading.value = false
  }
}

watch(() => props.symbolId, (id) => {
  if (id) fetchData()
  else rows.value = []
}, { immediate: true })
</script>

<style scoped>
.option-detail-root {
  display: flex;
  flex-direction: column;
  height: 100%;
  background: var(--bg-primary, #1e1e1e);
  color: var(--text-primary, #e0e0e0);
}
.detail-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 12px 14px;
  border-bottom: 1px solid var(--border, #333);
}
.contract-code {
  font-size: 15px;
  font-weight: 600;
  color: var(--primary, #4a9eff);
}
.contract-meta {
  display: flex;
  gap: 6px;
  margin-top: 4px;
  flex-wrap: wrap;
}
.meta-badge {
  font-size: 10px;
  padding: 2px 6px;
  border-radius: 8px;
  background: var(--bg-secondary, #2a2a2a);
  color: var(--text-secondary, #b0b0b0);
  border: 1px solid var(--border, #333);
}
.badge-call {
  background: rgba(245, 108, 108, 0.12);
  color: #f56c6c;
  border-color: rgba(245, 108, 108, 0.3);
}
.badge-put {
  background: rgba(103, 194, 58, 0.12);
  color: #67c23a;
  border-color: rgba(103, 194, 58, 0.3);
}
.btn-refresh {
  background: transparent;
  border: 1px solid var(--border, #333);
  color: var(--text-secondary, #b0b0b0);
  padding: 4px 8px;
  border-radius: 4px;
  cursor: pointer;
  font-size: 12px;
}
.btn-refresh:hover {
  color: var(--primary, #4a9eff);
  border-color: var(--primary, #4a9eff);
}
.detail-body {
  flex: 1;
  overflow-y: auto;
  padding: 12px 14px;
}
.summary-row {
  display: grid;
  grid-template-columns: repeat(3, 1fr);
  gap: 8px;
  margin-bottom: 12px;
}
.summary-card {
  padding: 8px 10px;
  background: var(--bg-secondary, #2a2a2a);
  border: 1px solid var(--border, #333);
  border-radius: 4px;
}
.summary-label {
  font-size: 10px;
  color: var(--text-secondary, #888);
  margin-bottom: 2px;
}
.summary-value {
  font-size: 14px;
  font-weight: 600;
}
.summary-value.small {
  font-size: 11px;
  font-weight: 500;
}
.detail-table {
  width: 100%;
  border-collapse: collapse;
  font-size: 12px;
}
.detail-table th,
.detail-table td {
  padding: 5px 8px;
  text-align: left;
  border-bottom: 1px solid var(--border, #333);
}
.detail-table th {
  font-weight: 500;
  color: var(--text-secondary, #b0b0b0);
  background: var(--bg-secondary, #252525);
  position: sticky;
  top: 0;
}
.detail-table td.num,
.detail-table th.num {
  text-align: right;
  font-variant-numeric: tabular-nums;
}
.detail-table tbody tr:hover {
  background: var(--hover-bg, rgba(255, 255, 255, 0.03));
}
.pagination-center {
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 4px;
  margin-top: 10px;
  padding: 6px 0;
}
.page-btn {
  background: transparent;
  border: 1px solid var(--border, #333);
  color: var(--text-secondary, #b0b0b0);
  padding: 3px 8px;
  border-radius: 3px;
  cursor: pointer;
  font-size: 11px;
}
.page-btn:disabled {
  opacity: 0.4;
  cursor: not-allowed;
}
.page-info {
  color: #999;
  font-size: 11px;
  margin: 0 4px;
}
.empty-tip,
.loading-tip,
.error-tip {
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  min-height: 200px;
  color: var(--text-secondary, #888);
  gap: 8px;
}
.empty-tip.small {
  min-height: 60px;
  padding: 16px;
}
.empty-tip i,
.loading-tip i,
.error-tip i {
  font-size: 24px;
}
.error-tip {
  color: #f56c6c;
}
</style>
