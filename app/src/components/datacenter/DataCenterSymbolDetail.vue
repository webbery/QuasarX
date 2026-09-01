<template>
  <div class="symbol-detail-root">
    <!-- 顶部标头 -->
    <div class="detail-header">
      <div class="header-info">
        <div class="symbol-code">{{ symbol || '—' }}</div>
        <div class="symbol-name">{{ symbolName || symbol ? '加载中…' : '' }}</div>
      </div>
      <div class="header-meta">
        <span class="freq-badge" :class="{ 'freq-daily': isDaily }">
          <i class="fas fa-clock"></i> {{ freqLabel }}
        </span>
      </div>
    </div>

    <!-- 内部 Tab -->
    <div class="detail-tabs">
      <button
        v-for="t in tabs"
        :key="t.id"
        class="tab-btn"
        :class="{ active: activeTab === t.id }"
        @click="onTabClick(t.id)"
      >
        <i :class="t.icon"></i> {{ t.label }}
      </button>
    </div>

    <!-- 内容区 -->
    <div class="detail-body">
      <div v-if="!symbol" class="empty-tip">
        <i class="fas fa-mouse-pointer"></i>
        <p>从左侧标的列表点击一行即可查看详情</p>
      </div>

      <div v-else-if="loading" class="loading-tip">
        <i class="fas fa-spinner fa-spin"></i> 加载中…
      </div>

      <div v-else-if="error" class="error-tip">
        <i class="fas fa-exclamation-triangle"></i> {{ error }}
      </div>

      <template v-else>
        <!-- 未复权 -->
        <div v-show="activeTab === 'quote'" class="tab-pane">
          <QuoteTable
            :bars="historyRaw"
            :freq-label="freqLabel"
            :is-daily="isDaily"
          />
        </div>

        <!-- 后复权 -->
        <div v-show="activeTab === 'hfq'" class="tab-pane">
          <div class="metric-row">
            <div class="metric-card">
              <div class="metric-label">最新复权因子</div>
              <div class="metric-value">
                {{ latestAdjFactor != null ? latestAdjFactor.toFixed(4) : '—' }}
              </div>
              <div class="metric-hint">后复权价 / 未复权价</div>
            </div>
            <div class="metric-card">
              <div class="metric-label">样本数</div>
              <div class="metric-value">{{ adjFactorSeries.length }}</div>
              <div class="metric-hint">与未复权时间对齐后</div>
            </div>
          </div>
          <QuoteTable
            :bars="historyHfq"
            :freq-label="freqLabel + '·后复权'"
            :is-daily="isDaily"
          />
        </div>

        <!-- 分红 -->
        <div v-show="activeTab === 'dividend'" class="tab-pane">
          <div class="metric-row">
            <div class="metric-card">
              <div class="metric-label">分红次数</div>
              <div class="metric-value">{{ dividendSummary.count }}</div>
            </div>
            <div class="metric-card">
              <div class="metric-label">累计派息(每10股)</div>
              <div class="metric-value">{{ dividendSummary.cash.toFixed(2) }} 元</div>
            </div>
            <div class="metric-card">
              <div class="metric-label">累计送股(每10股)</div>
              <div class="metric-value">
                {{ dividendSummary.bonus.toFixed(2) }} + 转 {{ dividendSummary.transfer.toFixed(2) }}
              </div>
            </div>
          </div>
          <div v-if="dividends.length === 0" class="empty-tip small">
            <i class="fas fa-info-circle"></i> 该标的暂无分红记录
          </div>
          <table v-else class="detail-table">
            <thead>
              <tr>
                <th>除权日</th>
                <th class="num">派息(每10股)</th>
                <th class="num">送股</th>
                <th class="num">转增</th>
                <th>公告日</th>
              </tr>
            </thead>
            <tbody>
              <tr v-for="(r, i) in dividends" :key="i">
                <td>{{ r.ex_dividend_date }}</td>
                <td class="num">{{ r.cash_per_10.toFixed(3) }}</td>
                <td class="num">{{ r.bonus_per_10.toFixed(2) }}</td>
                <td class="num">{{ r.transfer_per_10.toFixed(2) }}</td>
                <td>{{ r.announce_date || '—' }}</td>
              </tr>
            </tbody>
          </table>
        </div>

        <!-- 基本面 -->
        <div v-show="activeTab === 'finance'" class="tab-pane">
          <div v-if="Object.keys(finance).length === 0" class="empty-tip small">
            <i class="fas fa-info-circle"></i> 该标的暂无基本面数据
          </div>
          <template v-else>
            <div class="metric-row">
              <div v-for="card in financeCards" :key="card.label" class="metric-card">
                <div class="metric-label">{{ card.label }}</div>
                <div class="metric-value">
                  {{ card.value ?? '—' }}
                </div>
              </div>
            </div>
            <div v-for="cat in financeCategories" :key="cat.id" class="finance-section">
              <div class="finance-section-title">{{ cat.label }} · {{ finance[cat.id].length }} 条</div>
              <table v-if="finance[cat.id].length" class="detail-table compact">
                <thead>
                  <tr>
                    <th>报告期</th>
                    <th v-for="f in financeFields(cat.id)" :key="f">{{ f }}</th>
                  </tr>
                </thead>
                <tbody>
                  <tr v-for="(row, i) in finance[cat.id].slice().reverse().slice(0, 8)" :key="i">
                    <td>{{ row.stat_date?.slice(0, 10) }}</td>
                    <td v-for="f in financeFields(cat.id)" :key="f">{{ formatNum(row[f]) }}</td>
                  </tr>
                </tbody>
              </table>
            </div>
          </template>
        </div>
      </template>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, computed, watch } from 'vue'
import QuoteTable from './QuoteTable.vue'
import {
  useSymbolDetailData,
  getSymbolName,
  getFreqLabel,
  isDailyFreq,
  type DetailTab,
} from './composables/useSymbolDetailData'

const props = defineProps<{
  symbol: string | null
  freq: string
}>()

const {
  loading,
  error,
  activeTab,
  historyRaw,
  historyHfq,
  dividends,
  finance,
  setSymbol,
  switchTab,
  adjFactorSeries,
  latestAdjFactor,
  dividendSummary,
} = useSymbolDetailData()

const tabs: { id: DetailTab; label: string; icon: string }[] = [
  { id: 'quote',    label: '未复权', icon: 'fas fa-chart-line' },
  { id: 'hfq',      label: '后复权', icon: 'fas fa-chart-area' },
  { id: 'dividend', label: '分红',   icon: 'fas fa-coins' },
  { id: 'finance',  label: '基本面', icon: 'fas fa-file-invoice-dollar' },
]

const freqLabel = computed(() => getFreqLabel(props.freq || '1d'))
const isDaily = computed(() => isDailyFreq(props.freq || '1d'))

const symbolName = ref<string>('')
watch(
  () => props.symbol,
  (s) => {
    symbolName.value = s ? (getSymbolName(s) || '') : ''
    if (s) setSymbol(s, props.freq || '1d', activeTab.value)
  },
  { immediate: true },
)

function onTabClick(id: DetailTab) {
  if (activeTab.value === id) return
  switchTab(id)
}

// 6 类财务的中文标签
const financeCategories = [
  { id: 'profit',    label: '盈利能力' },
  { id: 'growth',    label: '成长能力' },
  { id: 'balance',   label: '偿债能力' },
  { id: 'cashflow',  label: '现金流量' },
  { id: 'operation', label: '营运能力' },
  { id: 'dupont',    label: '杜邦分析' },
]

// 关键指标卡片：每个类别取最新一行的代表字段
const financeCards = computed(() => {
  const map = finance.value
  const pick = (cat: string, field: string, label: string, suffix = '') => {
    const rows = map[cat]
    if (!rows || !rows.length) return { label, value: '—' }
    const v = rows[rows.length - 1][field]
    return { label, value: v == null ? '—' : `${formatNum(v)}${suffix}` }
  }
  return [
    pick('profit',    'roe_avg',     'ROE(平均)',     '%'),
    pick('profit',    'np_margin',   '净利润率',       '%'),
    pick('profit',    'gp_margin',   '毛利率',         '%'),
    pick('growth',    'mb_revenue',  '营收同比',       '%'),
    pick('balance',   'asset_liab',  '资产负债率',     '%'),
    pick('cashflow',  'cf_op',       '经营现金流(元)', ''),
  ]
})

function financeFields(cat: string): string[] {
  const rows = finance.value[cat]
  if (!rows?.length) return []
  // 排除日期列，按字段名排序取前 6 个
  const keys = Object.keys(rows[0]).filter(k => k !== 'id' && k !== 'symbol' && !k.endsWith('_date'))
  return keys.slice(0, 6)
}

function formatNum(v: any): string {
  if (v == null) return '—'
  const n = Number(v)
  if (!isFinite(n)) return String(v)
  if (Math.abs(n) >= 1e8) return (n / 1e8).toFixed(2) + '亿'
  if (Math.abs(n) >= 1e4) return (n / 1e4).toFixed(2) + '万'
  return n.toFixed(2)
}
</script>

<style scoped>
.symbol-detail-root {
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

.symbol-code {
  font-size: 16px;
  font-weight: 600;
  color: var(--primary, #4a9eff);
}

.symbol-name {
  font-size: 12px;
  color: var(--text-secondary, #b0b0b0);
  margin-top: 2px;
}

.freq-badge {
  display: inline-flex;
  align-items: center;
  gap: 4px;
  font-size: 11px;
  padding: 3px 8px;
  border-radius: 10px;
  background: var(--bg-secondary, #2a2a2a);
  color: var(--text-secondary, #b0b0b0);
  border: 1px solid var(--border, #333);
}

.freq-badge.freq-daily {
  background: rgba(74, 158, 255, 0.12);
  color: var(--primary, #4a9eff);
  border-color: var(--primary, #4a9eff);
}

.detail-tabs {
  display: flex;
  border-bottom: 1px solid var(--border, #333);
  flex-shrink: 0;
}

.tab-btn {
  flex: 1;
  padding: 8px 4px;
  background: transparent;
  border: none;
  color: var(--text-secondary, #b0b0b0);
  cursor: pointer;
  font-size: 12px;
  transition: all 0.15s;
  border-bottom: 2px solid transparent;
}

.tab-btn:hover {
  background: var(--hover-bg, rgba(255, 255, 255, 0.04));
  color: var(--text-primary, #e0e0e0);
}

.tab-btn.active {
  color: var(--primary, #4a9eff);
  border-bottom-color: var(--primary, #4a9eff);
}

.tab-btn i {
  margin-right: 4px;
}

.detail-body {
  flex: 1;
  overflow-y: auto;
  padding: 12px 14px;
}

.tab-pane {
  display: flex;
  flex-direction: column;
  gap: 12px;
}

.metric-row {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(140px, 1fr));
  gap: 8px;
}

.metric-card {
  padding: 10px 12px;
  background: var(--bg-secondary, #2a2a2a);
  border: 1px solid var(--border, #333);
  border-radius: 4px;
}

.metric-label {
  font-size: 11px;
  color: var(--text-secondary, #b0b0b0);
  margin-bottom: 4px;
}

.metric-value {
  font-size: 14px;
  font-weight: 600;
  color: var(--text-primary, #e0e0e0);
}

.metric-hint {
  font-size: 10px;
  color: var(--text-secondary, #888);
  margin-top: 2px;
}

.detail-table {
  width: 100%;
  border-collapse: collapse;
  font-size: 12px;
}

.detail-table th,
.detail-table td {
  padding: 6px 8px;
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

.detail-table.compact th,
.detail-table.compact td {
  padding: 4px 6px;
  font-size: 11px;
}

.detail-table td.num,
.detail-table th.num {
  text-align: right;
  font-variant-numeric: tabular-nums;
}

.detail-table tbody tr:hover {
  background: var(--hover-bg, rgba(255, 255, 255, 0.03));
}

.finance-section {
  margin-top: 8px;
}

.finance-section-title {
  font-size: 12px;
  font-weight: 600;
  color: var(--text-secondary, #b0b0b0);
  margin-bottom: 6px;
  padding-bottom: 4px;
  border-bottom: 1px dashed var(--border, #333);
}

.empty-tip,
.loading-tip,
.error-tip {
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  height: 100%;
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