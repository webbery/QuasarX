<template>
  <div class="feature-inspection" v-if="report">
    <!-- 汇总信息 -->
    <div class="summary-bar">
      <div class="summary-item">
        <span class="summary-label">总行数</span>
        <span class="summary-value">{{ report.total_rows }}</span>
      </div>
      <div class="summary-item">
        <span class="summary-label">特征数</span>
        <span class="summary-value">{{ report.n_features }}</span>
      </div>
      <div class="summary-item">
        <span class="summary-label">日期范围</span>
        <span class="summary-value">{{ report.date_start }} ~ {{ report.date_end }}</span>
      </div>
      <div class="summary-item">
        <span class="summary-label">问题特征</span>
        <span class="summary-value" :class="{ 'warn': problemCount > 0 }">
          {{ problemCount }} 个 NaN&gt;30%
        </span>
      </div>
    </div>

    <!-- 顶部选择器 -->
    <div class="selector-bar">
      <div class="selector-group">
        <label class="selector-label">Feature</label>
        <select v-model="selectedFeature" class="selector-input">
          <option v-for="f in availableFeatures" :key="f" :value="f">{{ f }}</option>
        </select>
      </div>
      <div class="selector-group selector-group-grow">
        <label class="selector-label">
          Symbols
          <span class="selector-hint">（≤5 只）</span>
        </label>
        <div class="symbol-chips">
          <label
            v-for="s in availableSymbols"
            :key="s"
            class="chip"
            :class="{ active: selectedSymbols.includes(s), disabled: !selectedSymbols.includes(s) && selectedSymbols.length >= 5 }"
          >
            <input
              type="checkbox"
              :value="s"
              :checked="selectedSymbols.includes(s)"
              :disabled="!selectedSymbols.includes(s) && selectedSymbols.length >= 5"
              @change="toggleSymbol(s)"
            />
            <span>{{ s }}</span>
          </label>
        </div>
      </div>
    </div>

    <!-- ① 折线图 -->
    <div class="chart-section">
      <div class="section-heading">
        <div>
          <span class="section-eyebrow">① 时序图</span>
          <h4 class="section-subtitle">{{ selectedFeature }} × {{ selectedSymbols.length }} 个标的</h4>
        </div>
        <span class="section-hint">NaN 处断线；红色 ↓0 标记跳 0 突变</span>
      </div>
      <FeatureSeriesChart
        v-if="chartData.series && Object.keys(chartData.series).length > 0"
        :dates="report.series.dates"
        :series="chartData.series"
        :anomalies="chartAnomalies"
        :symbol-field="selectedFeature"
      />
      <div v-else class="empty-mini">未选中任何标的</div>
    </div>

    <!-- ② 统计表 -->
    <div class="chart-section">
      <div class="section-heading">
        <div>
          <span class="section-eyebrow">② 统计表</span>
          <h4 class="section-subtitle">
            {{ selectedSymbols.length > 0 ? `${selectedSymbols.length} 个选中标的的特征统计` : '所有标的的特征统计' }}
          </h4>
        </div>
        <span class="section-hint">NaN% &gt; 30% 标红</span>
      </div>
      <div class="feature-table-wrapper">
        <table class="feature-table">
          <thead>
            <tr>
              <th>标的</th>
              <th>特征</th>
              <th>有效值</th>
              <th>NaN%</th>
              <th>最小值</th>
              <th>最大值</th>
              <th>均值</th>
              <th>标准差</th>
              <th>中位数</th>
              <th>状态</th>
            </tr>
          </thead>
          <tbody v-for="group in filteredGroupedFeatures" :key="group.symbol" class="symbol-group">
            <tr v-for="(f, idx) in group.features" :key="f.name" :class="rowClass(f)">
              <td v-if="idx === 0" :rowspan="group.features.length" class="symbol-cell">
                <div class="symbol-name">{{ group.symbol }}</div>
                <div class="symbol-count">({{ group.features.length }})</div>
              </td>
              <td class="feature-name" :title="f.name">{{ shortFeatureName(f.name) }}</td>
              <td>{{ f.valid }}/{{ f.valid + f.nan_count }}</td>
              <td :class="nanClass(f.nan_pct)">{{ f.nan_pct.toFixed(1) }}%</td>
              <td>{{ fmtNum(f.min) }}</td>
              <td>{{ fmtNum(f.max) }}</td>
              <td>{{ fmtNum(f.mean) }}</td>
              <td>{{ fmtNum(f.std) }}</td>
              <td>{{ fmtNum(f.median) }}</td>
              <td>{{ statusIcon(f) }}</td>
            </tr>
          </tbody>
        </table>
        <div v-if="filteredGroupedFeatures.length === 0" class="empty-mini">无匹配特征</div>
      </div>
    </div>

    <!-- ③ 异常报告 -->
    <div class="chart-section">
      <div class="section-heading">
        <div>
          <span class="section-eyebrow">③ 异常报告</span>
          <h4 class="section-subtitle">
            检测到 {{ allAnomalies.length }} 处异常
            <span class="severity-pills">
              <span v-if="severityCount.high" class="pill high">高 {{ severityCount.high }}</span>
              <span v-if="severityCount.mid" class="pill mid">中 {{ severityCount.mid }}</span>
              <span v-if="severityCount.low" class="pill low">低 {{ severityCount.low }}</span>
            </span>
          </h4>
        </div>
        <span class="section-hint">
          阈值：NaN≥{{ cfg.nanRunMin }} 天 / 跳0 后≥{{ cfg.jumpHoldMin }} 天 / 停滞≥{{ cfg.staleMin }} 天
        </span>
      </div>
      <div v-if="allAnomalies.length === 0" class="empty-mini">未检测到异常</div>
      <ul v-else class="anomaly-list">
        <li v-for="(a, i) in allAnomalies" :key="i" class="anomaly-item" :class="`sev-${a.severity}`">
          <span class="anomaly-type">{{ anomalyTypeLabel(a.type) }}</span>
          <span class="anomaly-meta">
            <span class="anomaly-sym">{{ a.symbol }}</span>
            <span class="anomaly-feat">.{{ a.feature }}</span>
          </span>
          <span class="anomaly-range">{{ a.start_date }} ~ {{ a.end_date }} ({{ a.length }} 天)</span>
          <span class="anomaly-detail">{{ a.detail }}</span>
        </li>
      </ul>
    </div>
  </div>
</template>

<script setup lang="ts">
import { computed, ref } from 'vue'
import type { FeatureReport, FeatureStat } from '../composables/useMLState'
import { detectAllAnomalies, parseFeatureName, DEFAULT_ANOMALY_CONFIG } from '../composables/useFeatureAnomaly'
import FeatureSeriesChart from '../charts/FeatureSeriesChart.vue'

const props = defineProps<{
  report: FeatureReport | null
}>()

const cfg = DEFAULT_ANOMALY_CONFIG

interface FeatureRow extends FeatureStat {
  symbol: string
  feature: string
}
interface SymbolGroup {
  symbol: string
  features: FeatureRow[]
  maxNanPct: number
}

const availableFeatures = computed<string[]>(() => {
  if (!props.report) return []
  const set = new Set<string>()
  for (const f of props.report.features) {
    set.add(parseFeatureName(f.name).feature)
  }
  return [...set].sort()
})

const availableSymbols = computed<string[]>(() => {
  if (!props.report || !selectedFeature.value) return []
  const set = new Set<string>()
  for (const f of props.report.features) {
    const { symbol, feature } = parseFeatureName(f.name)
    if (feature === selectedFeature.value && symbol) set.add(symbol)
  }
  return [...set].sort()
})

const selectedFeature = ref<string>('')
const selectedSymbols = ref<string[]>([])

// 当 feature 变化时重置 symbols 选中为前 5 个；symbols 变化时按上限裁剪
function onFeatureChange() {
  selectedSymbols.value = availableSymbols.value.slice(0, 5)
}
function toggleSymbol(s: string) {
  const idx = selectedSymbols.value.indexOf(s)
  if (idx >= 0) {
    selectedSymbols.value.splice(idx, 1)
  } else if (selectedSymbols.value.length < 5) {
    selectedSymbols.value.push(s)
  }
}

// 初始化：feature 默认第一个；symbols 默认前 5
const initialized = ref(false)
function ensureInit() {
  if (initialized.value || !props.report) return
  if (availableFeatures.value.length > 0) {
    selectedFeature.value = availableFeatures.value[0]
    onFeatureChange()
    initialized.value = true
  }
}
ensureInit()

const chartData = computed(() => {
  if (!props.report?.series?.data) return { series: {} as Record<string, (number | null)[]> }
  const out: Record<string, (number | null)[]> = {}
  for (const sym of selectedSymbols.value) {
    const key = `${sym}.${selectedFeature.value}`
    if (props.report!.series!.data[key]) {
      out[sym] = props.report!.series!.data[key]
    }
  }
  return { series: out }
})

const allAnomalies = computed(() => {
  if (!props.report?.series) return []
  return detectAllAnomalies(props.report.series, cfg)
})

// 只把选中标的 / 选中 feature 的异常投影到 chart
const chartAnomalies = computed(() => {
  const syms = new Set(selectedSymbols.value)
  return allAnomalies.value.filter(
    a => syms.has(a.symbol) && a.feature === selectedFeature.value,
  )
})

const severityCount = computed(() => {
  const c = { high: 0, mid: 0, low: 0 }
  for (const a of allAnomalies.value) c[a.severity]++
  return c
})

const problemCount = computed(() => {
  if (!props.report?.features) return 0
  return props.report.features.filter(f => f.nan_pct > 30).length
})

const filteredGroupedFeatures = computed<SymbolGroup[]>(() => {
  if (!props.report?.features) return []
  const featureFilter = selectedFeature.value
  const symbolFilter = new Set(selectedSymbols.value)
  const map = new Map<string, FeatureRow[]>()
  for (const f of props.report.features) {
    const { symbol, feature } = parseFeatureName(f.name)
    if (featureFilter && feature !== featureFilter) continue
    if (symbolFilter.size > 0 && !symbolFilter.has(symbol)) continue
    const row: FeatureRow = { ...f, symbol, feature }
    const arr = map.get(symbol)
    if (arr) arr.push(row)
    else map.set(symbol, [row])
  }
  const groups: SymbolGroup[] = []
  for (const [symbol, features] of map) {
    features.sort((a, b) => b.nan_pct - a.nan_pct)
    const maxNanPct = features.length > 0 ? features[0].nan_pct : 0
    groups.push({ symbol, features, maxNanPct })
  }
  groups.sort((a, b) => b.maxNanPct - a.maxNanPct)
  return groups
})

function shortFeatureName(full: string): string {
  const { feature } = parseFeatureName(full)
  return feature || full
}

function fmtNum(v: number | null): string {
  if (v == null) return '—'
  if (Math.abs(v) >= 1000) return v.toFixed(1)
  if (Math.abs(v) >= 1) return v.toFixed(3)
  if (v === 0) return '0'
  return v.toExponential(2)
}

function nanClass(pct: number): string {
  if (pct > 30) return 'nan-high'
  if (pct > 10) return 'nan-mid'
  return ''
}

function rowClass(f: FeatureStat): string {
  if (f.nan_pct > 30) return 'row-warn'
  if (f.nan_pct > 10) return 'row-caution'
  return ''
}

function statusIcon(f: FeatureStat): string {
  if (f.nan_pct > 50) return '❌'
  if (f.nan_pct > 30) return '⚠️'
  if (f.nan_pct > 10) return '🟡'
  return '✅'
}

function anomalyTypeLabel(t: string): string {
  if (t === 'nan_run') return 'NaN段'
  if (t === 'jump_to_zero') return '跳0'
  if (t === 'stale_run') return '停滞'
  return t
}
</script>

<style scoped>
.feature-inspection {
  display: flex;
  flex-direction: column;
  gap: 14px;
}

.summary-bar {
  display: flex;
  gap: 24px;
  padding: 10px 16px;
  background: rgba(15, 25, 41, 0.5);
  border: 1px solid rgba(74, 85, 104, 0.25);
  border-radius: 6px;
}
.summary-item { display: flex; flex-direction: column; gap: 2px; }
.summary-label {
  font-size: 11px;
  color: #94a3b8;
  text-transform: uppercase;
  letter-spacing: 0.5px;
}
.summary-value { font-size: 14px; font-weight: 600; color: #e2e8f0; }
.summary-value.warn { color: #f59e0b; }

.selector-bar {
  display: flex;
  gap: 16px;
  padding: 12px 16px;
  background: rgba(15, 25, 41, 0.55);
  border: 1px solid rgba(74, 85, 104, 0.25);
  border-radius: 6px;
  align-items: flex-start;
}
.selector-group { display: flex; flex-direction: column; gap: 6px; flex-shrink: 0; }
.selector-group-grow { flex: 1; }
.selector-label {
  font-size: 11px;
  color: #94a3b8;
  text-transform: uppercase;
  letter-spacing: 0.5px;
  font-weight: 600;
}
.selector-hint { color: #64748b; font-weight: 400; text-transform: none; letter-spacing: 0; }
.selector-input {
  padding: 4px 8px;
  background: rgba(15, 25, 41, 0.7);
  border: 1px solid rgba(74, 85, 104, 0.4);
  border-radius: 4px;
  color: #e2e8f0;
  font-size: 12px;
  min-width: 160px;
  font-family: 'SF Mono', 'Consolas', monospace;
}

.symbol-chips {
  display: flex;
  flex-wrap: wrap;
  gap: 6px;
  max-height: 84px;
  overflow-y: auto;
}
.chip {
  display: inline-flex;
  align-items: center;
  gap: 4px;
  padding: 3px 9px;
  font-size: 11px;
  font-family: 'SF Mono', 'Consolas', monospace;
  background: rgba(74, 85, 104, 0.15);
  border: 1px solid rgba(74, 85, 104, 0.3);
  border-radius: 999px;
  cursor: pointer;
  color: #94a3b8;
  transition: all 0.15s;
  user-select: none;
}
.chip input { display: none; }
.chip:hover { background: rgba(91, 143, 249, 0.15); border-color: rgba(91, 143, 249, 0.4); color: #e2e8f0; }
.chip.active { background: rgba(91, 143, 249, 0.25); border-color: #5b8ff9; color: #93c5fd; }
.chip.disabled { opacity: 0.4; cursor: not-allowed; }
.chip.disabled:hover { background: rgba(74, 85, 104, 0.15); border-color: rgba(74, 85, 104, 0.3); color: #94a3b8; }

.chart-section {
  background: rgba(15, 25, 41, 0.55);
  border: 1px solid rgba(74, 85, 104, 0.25);
  border-radius: 8px;
  padding: 12px 14px 14px;
}
.section-heading {
  display: flex;
  justify-content: space-between;
  align-items: flex-end;
  margin-bottom: 10px;
  gap: 12px;
  flex-wrap: wrap;
}
.section-eyebrow {
  font-size: 10.5px;
  letter-spacing: 2.2px;
  color: #5b8ff9;
  font-weight: 600;
  text-transform: uppercase;
}
.section-subtitle {
  margin: 4px 0 0;
  font-size: 13px;
  font-weight: 600;
  color: #e2e8f0;
}
.section-hint { font-size: 11px; color: #94a3b8; }

.severity-pills { display: inline-flex; gap: 6px; margin-left: 8px; vertical-align: middle; }
.pill {
  display: inline-block;
  padding: 1px 8px;
  font-size: 10.5px;
  border-radius: 999px;
  font-weight: 600;
}
.pill.high { background: rgba(239, 68, 68, 0.18); color: #f87171; }
.pill.mid { background: rgba(245, 158, 11, 0.18); color: #fbbf24; }
.pill.low { background: rgba(148, 163, 184, 0.18); color: #cbd5e1; }

.empty-mini {
  padding: 30px 20px;
  text-align: center;
  color: #64748b;
  font-size: 12px;
}

.feature-table-wrapper {
  overflow-x: auto;
  border: 1px solid rgba(74, 85, 104, 0.25);
  border-radius: 6px;
}
.feature-table { width: 100%; border-collapse: collapse; font-size: 12px; }
.feature-table th {
  padding: 8px 10px;
  text-align: left;
  font-weight: 600;
  color: #94a3b8;
  background: rgba(15, 25, 41, 0.6);
  border-bottom: 1px solid rgba(74, 85, 104, 0.3);
  white-space: nowrap;
}
.feature-table td {
  padding: 6px 10px;
  border-bottom: 1px solid rgba(74, 85, 104, 0.15);
  color: #cbd5e1;
  white-space: nowrap;
}
.feature-table tr:hover { background: rgba(59, 130, 246, 0.05); }
.feature-name {
  max-width: 160px;
  overflow: hidden;
  text-overflow: ellipsis;
  font-family: monospace;
  font-size: 11px;
}
.symbol-cell {
  vertical-align: middle;
  border-right: 2px solid rgba(74, 85, 104, 0.35);
  background: rgba(15, 25, 41, 0.3);
  text-align: center;
  padding: 6px 12px;
}
.symbol-name { font-family: monospace; font-size: 12px; font-weight: 600; color: #e2e8f0; white-space: nowrap; }
.symbol-count { font-size: 10px; color: #64748b; margin-top: 2px; }
.symbol-group + .symbol-group { border-top: 2px solid rgba(74, 85, 104, 0.35); }
.row-warn { background: rgba(239, 68, 68, 0.06); }
.row-caution { background: rgba(245, 158, 11, 0.06); }
.nan-high { color: #ef4444; font-weight: 600; }
.nan-mid { color: #f59e0b; }

.anomaly-list {
  list-style: none;
  margin: 0;
  padding: 0;
  display: flex;
  flex-direction: column;
  gap: 6px;
  max-height: 320px;
  overflow-y: auto;
}
.anomaly-item {
  display: grid;
  grid-template-columns: 60px 200px 220px 1fr;
  gap: 10px;
  align-items: center;
  padding: 7px 12px;
  background: rgba(15, 25, 41, 0.5);
  border-left: 3px solid #475569;
  border-radius: 4px;
  font-size: 12px;
}
.anomaly-item.sev-high { border-left-color: #ef4444; background: rgba(239, 68, 68, 0.05); }
.anomaly-item.sev-mid { border-left-color: #f59e0b; background: rgba(245, 158, 11, 0.05); }
.anomaly-item.sev-low { border-left-color: #94a3b8; }
.anomaly-type {
  font-weight: 600;
  font-size: 11px;
  padding: 2px 6px;
  border-radius: 3px;
  background: rgba(91, 143, 249, 0.15);
  color: #93c5fd;
  text-align: center;
}
.anomaly-meta { font-family: monospace; color: #cbd5e1; font-size: 11.5px; }
.anomaly-sym { color: #e2e8f0; font-weight: 600; }
.anomaly-feat { color: #94a3b8; }
.anomaly-range { color: #94a3b8; font-size: 11px; font-family: 'SF Mono', 'Consolas', monospace; }
.anomaly-detail { color: #94a3b8; font-size: 11.5px; }
</style>
