<template>
  <div class="signal-tab">
    <!-- 顶部控制栏 -->
    <div class="control-bar">
      <!-- 策略选择 -->
      <div class="strategy-selector">
        <label>策略:</label>
        <select v-model="selectedStrategyId" class="select-small" @change="onStrategyChange">
          <option value="">请选择策略</option>
          <option v-for="opt in strategyOptions" :key="opt.id" :value="opt.id">{{ opt.name }}</option>
        </select>
      </div>

      <!-- 标的池 (默认展开) -->
      <div v-if="availableSecurities.length > 0" class="symbol-pool">
        <span class="pool-label">标的池:</span>
        <label
          v-for="sec in availableSecurities"
          :key="sec.code"
          class="pool-checkbox"
        >
          <input
            type="checkbox"
            :checked="checkedSymbols.has(sec.code)"
            @change="toggleSymbol(sec.code)"
          />
          <span>{{ sec.name ? `${sec.code}(${sec.name})` : sec.code }}</span>
        </label>
      </div>
      <div v-else-if="selectedStrategyId && !loading" class="pool-empty">
        该策略未找到行情输入节点
      </div>

      <!-- 已选标的 tag -->
      <div v-if="state.symbols.length > 0" class="selected-symbols">
        <span class="selected-label">已选:</span>
        <span
          v-for="(sym, idx) in state.symbols"
          :key="sym.symbol"
          class="symbol-tag"
        >
          {{ sym.symbol }}
          <button class="tag-close" @click="removeSymbol(idx)">×</button>
        </span>
      </div>

      <div class="control-spacer" />

      <!-- 分析字段 -->
      <div class="field-selector">
        <label>字段:</label>
        <select v-model="state.field" class="select-small">
          <option value="close">C 收盘价</option>
          <option value="open">O 开盘价</option>
          <option value="high">H 最高价</option>
          <option value="low">L 最低价</option>
          <option value="volume">V 成交量</option>
          <option value="turnover">T 换手率</option>
        </select>
      </div>

      <!-- 分解方法 -->
      <div class="method-selector">
        <label>方法:</label>
        <select v-model="state.method" class="select-small">
          <option value="emd">EMD</option>
          <option value="ceemdan">CEEMDAN</option>
        </select>
      </div>

      <!-- IMF数量 -->
      <div class="imf-count-selector">
        <label>IMF:</label>
        <input
          v-model.number="state.numImfs"
          type="number"
          min="1"
          max="20"
          class="number-input-small"
        />
      </div>

      <!-- 时间范围 -->
      <div class="time-selector">
        <select v-model="state.quickRange" class="select-small" @change="setQuickRange(state.quickRange)">
          <option v-for="[label] in QUICK_RANGES" :key="label" :value="label">{{ label }}</option>
        </select>
        <div class="date-range-inline">
          <input type="date" :value="state.dateRange?.[0]" @input="updateStartDate($event)" class="date-input-small" placeholder="开始" />
          <span class="date-sep">至</span>
          <input type="date" :value="state.dateRange?.[1]" @input="updateEndDate($event)" class="date-input-small" placeholder="结束" />
        </div>
      </div>

      <button class="btn btn-primary btn-small" :disabled="loading || state.symbols.length === 0" @click="runAnalysis">
        {{ loading ? '分析中...' : '开始分析' }}
      </button>
    </div>

    <!-- 分析结果 -->
    <div v-if="state.result" class="results">
      <section class="section">
        <h3 class="section-title">EMD 分解</h3>
        <div class="chart-grid">
          <div class="chart-card full">
            <EMDDecompositionChart :data="state.result" />
          </div>
        </div>

        <div class="chart-grid">
          <div class="chart-card half">
            <IMFEnergyChart :data="state.result" />
          </div>
          <div class="chart-card half">
            <!-- 预留：多标的 IMF 相关性热力图 -->
            <div class="placeholder-card">
              <div class="placeholder-text">多标的 IMF 相关性（标的数 ≥ 2 时显示）</div>
            </div>
          </div>
        </div>
      </section>
    </div>

    <!-- 空状态 -->
    <div v-else class="empty-state">
      <div class="empty-content">
        <div class="empty-icon">🔬</div>
        <div class="empty-text">请添加标的并点击「开始分析」</div>
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, computed, watch } from 'vue'
import { useSignalState, useSignalData } from './index'
import { useStrategySecurities } from '../shared/composables/useStrategySecurities'
import EMDDecompositionChart from './charts/EMDDecompositionChart.vue'
import IMFEnergyChart from './charts/IMFEnergyChart.vue'

const { state, QUICK_RANGES, removeSymbol, setQuickRange } = useSignalState()
const { fetchSignal } = useSignalData()
const {
  strategyOptions,
  selectedStrategyId,
  availableSecurities,
  checkedSymbols,
  loading: securitiesLoading,
  loadSecuritiesForStrategy,
  toggleSymbol,
} = useStrategySecurities()

const analysisLoading = ref(false)
const loading = computed(() => securitiesLoading.value || analysisLoading.value)

function onStrategyChange() {
  if (selectedStrategyId.value) {
    loadSecuritiesForStrategy(selectedStrategyId.value)
  } else {
    availableSecurities.value = []
    checkedSymbols.value = new Set()
    state.symbols = []
  }
}

// 监听勾选变化，同步到 state.symbols
watch(checkedSymbols, (next) => {
  const current = new Set(state.symbols.map(s => s.symbol))
  const toAdd = Array.from(next).filter(c => !current.has(c))

  for (const code of toAdd) {
    state.symbols.push({ symbol: code })
  }
  state.symbols = state.symbols.filter(s => next.has(s.symbol))
}, { deep: false })

function updateStartDate(event: Event) {
  const value = (event.target as HTMLInputElement).value
  if (state.dateRange) {
    state.dateRange = [value, state.dateRange[1]]
  }
}

function updateEndDate(event: Event) {
  const value = (event.target as HTMLInputElement).value
  if (state.dateRange) {
    state.dateRange = [state.dateRange[0], value]
  }
}

async function runAnalysis() {
  if (!state.dateRange || state.symbols.length === 0) return

  analysisLoading.value = true
  try {
    const symbols = state.symbols.map(s => s.symbol)
    const [start_date, end_date] = state.dateRange
    const field = state.field || 'close'

    const result = await fetchSignal(symbols, start_date, end_date, field, state.method, state.numImfs)
    if (result) {
      state.result = result
    }
  } finally {
    analysisLoading.value = false
  }
}
</script>

<style scoped>
.signal-tab {
  display: flex;
  flex-direction: column;
  height: 100%;
  background: #1a2236;
  color: #e0e0e0;
}

.control-bar {
  display: flex;
  align-items: center;
  gap: 12px;
  padding: 10px 16px;
  background: rgba(26, 34, 54, 0.8);
  border-bottom: 1px solid rgba(74, 85, 104, 0.3);
  flex-wrap: wrap;
}

.strategy-selector {
  display: flex;
  align-items: center;
  gap: 6px;
}

.strategy-selector label {
  font-size: 12px;
  color: #999;
  white-space: nowrap;
}

/* 标的池 */
.symbol-pool {
  display: flex;
  align-items: center;
  gap: 10px;
  flex-wrap: wrap;
  padding: 4px 8px;
  background: rgba(41, 98, 255, 0.05);
  border: 1px solid rgba(74, 85, 104, 0.2);
  border-radius: 4px;
}

.pool-label {
  font-size: 12px;
  color: #999;
  white-space: nowrap;
}

.pool-checkbox {
  display: inline-flex;
  align-items: center;
  gap: 4px;
  font-size: 12px;
  color: #e0e0e0;
  cursor: pointer;
  user-select: none;
}

.pool-checkbox input[type="checkbox"] {
  accent-color: #2962ff;
  margin: 0;
  cursor: pointer;
}

.pool-checkbox input[type="checkbox"]:checked + span {
  color: #2962ff;
  font-weight: 500;
}

.pool-empty {
  font-size: 12px;
  color: #666;
  font-style: italic;
}

/* 已选标的 */
.selected-symbols {
  display: flex;
  align-items: center;
  gap: 6px;
  flex-wrap: wrap;
}

.selected-label {
  font-size: 12px;
  color: #999;
  white-space: nowrap;
}

.symbol-tag {
  display: inline-flex;
  align-items: center;
  gap: 4px;
  padding: 3px 8px;
  background: rgba(41, 98, 255, 0.2);
  border: 1px solid rgba(41, 98, 255, 0.4);
  border-radius: 4px;
  font-size: 12px;
  color: #e0e0e0;
}

.tag-close {
  background: transparent;
  border: none;
  color: #999;
  font-size: 14px;
  line-height: 1;
  cursor: pointer;
  padding: 0 2px;
  margin-left: 2px;
}

.tag-close:hover {
  color: #ef232a;
}

.control-spacer {
  flex: 1;
  min-width: 8px;
}

.field-selector,
.method-selector,
.imf-count-selector {
  display: flex;
  align-items: center;
  gap: 6px;
}

.field-selector label,
.method-selector label,
.imf-count-selector label {
  font-size: 12px;
  color: #999;
  white-space: nowrap;
}

.number-input-small {
  padding: 4px 8px;
  background: rgba(26, 34, 54, 0.8);
  border: 1px solid rgba(74, 85, 104, 0.3);
  border-radius: 4px;
  color: #e0e0e0;
  font-size: 12px;
  outline: none;
  width: 50px;
  text-align: center;
}

.number-input-small:focus {
  border-color: rgba(41, 98, 255, 0.5);
}

.time-selector {
  display: flex;
  align-items: center;
  gap: 8px;
}

.date-range-inline {
  display: flex;
  align-items: center;
  gap: 6px;
}

.date-input-small {
  padding: 4px 8px;
  background: rgba(26, 34, 54, 0.8);
  border: 1px solid rgba(74, 85, 104, 0.3);
  border-radius: 4px;
  color: #e0e0e0;
  font-size: 12px;
  outline: none;
  width: 120px;
}

.date-input-small:focus {
  border-color: rgba(41, 98, 255, 0.5);
}

.date-input-small::-webkit-calendar-picker-indicator {
  filter: invert(1);
  cursor: pointer;
}

.date-sep {
  color: #999;
  font-size: 12px;
}

.select-small {
  padding: 4px 8px;
  background: rgba(26, 34, 54, 0.8);
  border: 1px solid rgba(74, 85, 104, 0.3);
  border-radius: 4px;
  color: #e0e0e0;
  font-size: 12px;
  outline: none;
  cursor: pointer;
}

.select-small:focus {
  border-color: rgba(41, 98, 255, 0.5);
}

.select-small option {
  background: #1a2236;
  color: #e0e0e0;
}

.strategy-link {
  display: flex;
  align-items: center;
  gap: 8px;
}

.btn {
  padding: 6px 16px;
  border: 1px solid rgba(74, 85, 104, 0.3);
  border-radius: 4px;
  background: rgba(26, 34, 54, 0.8);
  color: #e0e0e0;
  font-size: 14px;
  cursor: pointer;
  transition: all 0.2s;
}

.btn:hover:not(:disabled) {
  border-color: rgba(41, 98, 255, 0.5);
}

.btn:disabled {
  opacity: 0.5;
  cursor: not-allowed;
}

.btn-primary {
  background: #2962ff;
  border-color: #2962ff;
  font-weight: 600;
}

.btn-primary:hover:not(:disabled) {
  background: #1e54e6;
  border-color: #1e54e6;
}

.btn-small {
  padding: 4px 12px;
  font-size: 12px;
}

.results {
  flex: 1;
  overflow: auto;
  padding: 16px;
}

.section {
  margin-bottom: 24px;
}

.section-title {
  margin: 0 0 12px 0;
  font-size: 16px;
  color: #e0e0e0;
  font-weight: 600;
  padding-left: 12px;
  border-left: 3px solid #2962ff;
}

.chart-grid {
  display: flex;
  gap: 16px;
  margin-bottom: 16px;
}

.chart-card {
  background: rgba(26, 34, 54, 0.5);
  border: 1px solid rgba(74, 85, 104, 0.2);
  border-radius: 8px;
  padding: 12px;
  min-height: 300px;
}

.chart-card.half {
  flex: 1;
}

.chart-card.full {
  flex: 1;
  min-height: 600px;
}

.placeholder-card {
  display: flex;
  align-items: center;
  justify-content: center;
  min-height: 300px;
}

.placeholder-text {
  color: #999;
  font-size: 13px;
}

.empty-state {
  flex: 1;
  display: flex;
  align-items: center;
  justify-content: center;
}

.empty-content {
  text-align: center;
  color: #999;
}

.empty-icon {
  font-size: 48px;
  margin-bottom: 12px;
}

.empty-text {
  font-size: 14px;
}

@media (max-width: 1200px) {
  .chart-grid {
    flex-direction: column;
  }
  .chart-card.half {
    min-height: 300px;
  }
}
</style>
