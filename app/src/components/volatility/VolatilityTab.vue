<template>
  <div class="volatility-tab">
    <!-- 顶部控制栏 -->
    <div class="control-bar">
      <!-- 模式切换 -->
      <div class="mode-toggle">
        <button
          :class="['mode-btn', { active: mode === 'strategy' }]"
          @click="switchMode('strategy')"
        >策略行情</button>
        <button
          :class="['mode-btn', { active: mode === 'macro' }]"
          @click="switchMode('macro')"
        >宏观指标</button>
      </div>

      <!-- 策略模式：策略选择 + 标的下拉选择 -->
      <template v-if="mode === 'strategy'">
        <div class="strategy-selector">
          <label>策略:</label>
          <select v-model="selectedStrategyId" class="select-small" @change="onStrategyChange">
            <option value="">请选择策略</option>
            <option v-for="opt in strategyOptions" :key="opt.id" :value="opt.id">{{ opt.name }}</option>
          </select>
        </div>

        <!-- 标的多选下拉框 -->
        <div v-if="availableSecurities.length > 0" class="symbol-multiselect-dropdown" ref="symbolDropdownRef">
          <label>标的:</label>
          <div class="multiselect-trigger" @click="toggleSymbolDropdown">
            <span class="multiselect-display">
              {{ state.symbols.length > 0 ? `已选 ${state.symbols.length} 个` : '选择标的' }}
            </span>
            <span class="multiselect-arrow">▼</span>
          </div>
          <div v-if="symbolDropdownOpen" class="multiselect-dropdown-content">
            <label
              v-for="sec in availableSecurities"
              :key="sec.code"
              class="multiselect-checkbox"
            >
              <input
                type="checkbox"
                :checked="checkedSymbols.has(sec.code)"
                @change="toggleSymbol(sec.code)"
              />
              <span>{{ sec.name ? `${sec.code}(${sec.name})` : sec.code }}</span>
            </label>
          </div>
        </div>
        <div v-else-if="selectedStrategyId && !loading" class="pool-empty">
          该策略未找到行情输入节点
        </div>
      </template>

      <!-- 宏观模式：国家 + 指标级联选择 -->
      <template v-else>
        <div class="macro-selector">
          <label>国家:</label>
          <select v-model="selectedMacroCountry" class="select-small" @change="onMacroCountryChange">
            <option value="china">中国</option>
            <option value="usa">美国</option>
            <option value="global">全球</option>
          </select>
        </div>
        <div class="macro-selector">
          <label>指标:</label>
          <select v-model="selectedMacroIndicator" class="select-small">
            <option value="">请选择指标</option>
            <option v-for="opt in filteredMacroOptions" :key="opt.indicator" :value="opt.indicator">
              {{ opt.label }}
            </option>
          </select>
        </div>
      </template>

      <div class="control-spacer" />

      <!-- 分析字段 (宏观模式隐藏，因为宏观数据只有 value 列) -->
      <div v-if="mode === 'strategy'" class="field-selector">
        <label>字段:</label>
        <select v-model="state.field" class="select-small">
          <option value="close">C 收盘价</option>
          <option value="open">O 开盘价</option>
          <option value="high">H 最高价</option>
          <option value="low">L 最低价</option>
          <option value="volume">V 成交量</option>
        </select>
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

      <button class="btn btn-primary btn-small" :disabled="loading || !canAnalyze" @click="runAnalysis">
        {{ loading ? '分析中...' : '开始分析' }}
      </button>
    </div>

    <!-- 分析结果 -->
    <div v-if="state.result" class="results">
      <!-- 单标的分析 -->
      <section v-if="state.symbols.length >= 1" class="section">
        <div class="section-header">
          <h3 class="section-title">单标的分析</h3>
          <div v-if="state.symbols.length > 1" class="symbol-selector-inline">
            <label>当前标的:</label>
            <select v-model="currentSymbolIndex" class="select-small">
              <option v-for="(sym, idx) in state.symbols" :key="sym.symbol" :value="idx">
                {{ sym.symbol }}
              </option>
            </select>
          </div>
        </div>
        <div class="chart-grid">
          <div class="chart-card half">
            <ReturnDistributionChart :data="currentSingleResult" />
          </div>
          <div class="chart-card half">
            <RollingVolatilityChart :data="currentSingleResult" :windows="state.windows" :dates="state.result.dates" />
          </div>
        </div>

        <div class="chart-grid">
          <div class="chart-card full">
            <PriceBandChart :data="currentSingleResult" :dates="state.result.dates" />
          </div>
        </div>

        <div class="chart-grid">
          <div class="chart-card half">
            <VolatilityClusteringChart :data="currentSingleResult" />
          </div>
          <div class="chart-card half">
            <AutocorrelationChart :data="currentSingleResult" />
          </div>
        </div>

        <div class="chart-grid">
          <div class="chart-card full">
            <MetricsCard :data="currentSingleResult" />
          </div>
        </div>
      </section>

      <!-- 多标的关联分析 -->
      <section v-if="state.symbols.length >= 2" class="section">
        <h3 class="section-title">多标的关联分析</h3>
        <div class="chart-grid">
          <div class="chart-card full">
            <CorrelationHeatmapChart
              :symbols="state.result.symbols"
              :correlation-matrix="state.result.multi.correlation_matrix"
            />
          </div>
        </div>

        <div class="chart-grid">
          <div class="chart-card half">
            <VolatilityComparisonChart
              :symbols="state.result.symbols"
              :annual-volatility="state.result.multi.annual_volatility"
            />
          </div>
          <div class="chart-card half">
            <CovarianceEigenChart :data="state.result.multi" />
          </div>
        </div>
      </section>
    </div>

    <!-- 空状态 -->
    <div v-else class="empty-state">
      <div class="empty-content">
        <div class="empty-icon">📊</div>
        <div class="empty-text">请添加标的并点击「开始分析」</div>
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, computed, watch, onMounted, onUnmounted } from 'vue'
import { useVolatilityState, useVolatilityData } from './index'
import { useStrategySecurities } from '../shared/composables/useStrategySecurities'
import { useMacroIndicators } from '../shared/composables/useMacroIndicators'
import ReturnDistributionChart from './charts/ReturnDistributionChart.vue'
import RollingVolatilityChart from './charts/RollingVolatilityChart.vue'
import PriceBandChart from './charts/PriceBandChart.vue'
import VolatilityClusteringChart from './charts/VolatilityClusteringChart.vue'
import AutocorrelationChart from './charts/AutocorrelationChart.vue'
import MetricsCard from './charts/MetricsCard.vue'
import CorrelationHeatmapChart from './charts/CorrelationHeatmapChart.vue'
import VolatilityComparisonChart from './charts/VolatilityComparisonChart.vue'
import CovarianceEigenChart from './charts/CovarianceEigenChart.vue'

const { state, QUICK_RANGES, removeSymbol, setQuickRange } = useVolatilityState()
const { fetchVolatility } = useVolatilityData()
const {
  strategyOptions,
  selectedStrategyId,
  availableSecurities,
  checkedSymbols,
  loading: securitiesLoading,
  loadSecuritiesForStrategy,
  toggleSymbol,
} = useStrategySecurities()
const { macroOptionsByCountry } = useMacroIndicators()

// === 模式切换 ===
type AnalysisMode = 'strategy' | 'macro'
const mode = ref<AnalysisMode>('strategy')
const selectedMacroCountry = ref('china')
const selectedMacroIndicator = ref('')
const selectedSymbolForAdd = ref('')
const symbolDropdownOpen = ref(false)
const symbolDropdownRef = ref<HTMLElement | null>(null)

const filteredMacroOptions = computed(() =>
  macroOptionsByCountry.value[selectedMacroCountry.value] || []
)

const analysisLoading = ref(false)
const loading = computed(() => securitiesLoading.value || analysisLoading.value)

// 是否可以执行分析
const canAnalyze = computed(() => {
  if (loading.value) return false
  if (mode.value === 'macro') {
    return !!selectedMacroIndicator.value
  }
  return state.symbols.length > 0
})

function switchMode(newMode: AnalysisMode) {
  mode.value = newMode
  symbolDropdownOpen.value = false
  // 清空旧模式的状态
  if (newMode === 'strategy') {
    selectedMacroIndicator.value = ''
    state.symbols = []
    selectedSymbolForAdd.value = ''
  } else {
    selectedStrategyId.value = ''
    availableSecurities.value = []
    checkedSymbols.value = new Set()
    state.symbols = []
    selectedSymbolForAdd.value = ''
    // 默认选中第一个指标
    const opts = filteredMacroOptions.value
    if (opts.length > 0) {
      selectedMacroIndicator.value = opts[0].indicator
    }
  }
}

function toggleSymbolDropdown() {
  symbolDropdownOpen.value = !symbolDropdownOpen.value
}

// 点击外部关闭下拉框
function handleClickOutside(event: MouseEvent) {
  if (symbolDropdownRef.value && !symbolDropdownRef.value.contains(event.target as Node)) {
    symbolDropdownOpen.value = false
  }
}

onMounted(() => {
  document.addEventListener('click', handleClickOutside)
})

onUnmounted(() => {
  document.removeEventListener('click', handleClickOutside)
})

function onMacroCountryChange() {
  selectedMacroIndicator.value = ''
  const opts = filteredMacroOptions.value
  if (opts.length > 0) {
    selectedMacroIndicator.value = opts[0].indicator
  }
}

function onStrategyChange() {
  if (selectedStrategyId.value) {
    loadSecuritiesForStrategy(selectedStrategyId.value)
  } else {
    availableSecurities.value = []
    checkedSymbols.value = new Set()
    state.symbols = []
    selectedSymbolForAdd.value = ''
  }
}

function onSymbolSelect() {
  if (selectedSymbolForAdd.value && !state.symbols.some(s => s.symbol === selectedSymbolForAdd.value)) {
    state.symbols.push({ symbol: selectedSymbolForAdd.value })
    selectedSymbolForAdd.value = '' // 清空选择
  }
}

// 监听勾选变化，同步到 state.symbols
watch(checkedSymbols, (next) => {
  if (mode.value !== 'strategy') return
  const current = new Set(state.symbols.map(s => s.symbol))
  const toAdd = Array.from(next).filter(c => !current.has(c))
  const toRemove = Array.from(current).filter(c => !next.has(c))

  for (const code of toAdd) {
    state.symbols.push({ symbol: code })
  }
  state.symbols = state.symbols.filter(s => next.has(s.symbol))
}, { deep: false })

// 监听宏观指标选择变化
watch([selectedMacroCountry, selectedMacroIndicator], ([country, indicator]) => {
  if (mode.value !== 'macro' || !indicator) {
    return
  }
  const macroSymbol = `${country}/${indicator}`
  state.symbols = [{ symbol: macroSymbol }]
})

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

// 当前选中的单标的结果（默认第一个）
const currentSymbolIndex = ref(0)
const currentSingleResult = computed(() => {
  if (!state.result?.single) return null
  const sym = state.symbols[currentSymbolIndex.value]?.symbol
  return sym ? state.result.single[sym] : null
})

async function runAnalysis() {
  if (!state.dateRange || state.symbols.length === 0) return

  analysisLoading.value = true
  try {
    const symbols = state.symbols.map(s => s.symbol)
    const [start_date, end_date] = state.dateRange
    const field = mode.value === 'macro' ? 'value' : (state.field || 'close')

    const result = await fetchVolatility(symbols, start_date, end_date, state.windows, field)
    if (result) {
      state.result = result
    }
  } finally {
    analysisLoading.value = false
  }
}
</script>

<style scoped>
.volatility-tab {
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
  min-height: 52px;
}

/* 模式切换按钮 */
.mode-toggle {
  display: flex;
  border: 1px solid rgba(74, 85, 104, 0.3);
  border-radius: 4px;
  overflow: hidden;
}

.mode-btn {
  padding: 4px 12px;
  background: rgba(26, 34, 54, 0.8);
  border: none;
  border-right: 1px solid rgba(74, 85, 104, 0.3);
  color: #999;
  font-size: 12px;
  cursor: pointer;
  transition: all 0.2s;
}

.mode-btn:last-child {
  border-right: none;
}

.mode-btn:hover {
  color: #e0e0e0;
}

.mode-btn.active {
  background: #2962ff;
  color: #fff;
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

/* 标的多选下拉框 */
.symbol-multiselect-dropdown {
  position: relative;
  display: flex;
  align-items: center;
  gap: 6px;
}

.symbol-multiselect-dropdown label {
  font-size: 12px;
  color: #999;
  white-space: nowrap;
}

.multiselect-trigger {
  display: flex;
  align-items: center;
  gap: 6px;
  padding: 4px 8px;
  background: rgba(26, 34, 54, 0.8);
  border: 1px solid rgba(74, 85, 104, 0.3);
  border-radius: 4px;
  cursor: pointer;
  min-width: 120px;
  transition: border-color 0.2s;
}

.multiselect-trigger:hover {
  border-color: rgba(41, 98, 255, 0.5);
}

.multiselect-display {
  font-size: 12px;
  color: #e0e0e0;
}

.multiselect-arrow {
  font-size: 8px;
  color: #999;
  transition: transform 0.2s;
}

.multiselect-dropdown-content {
  position: absolute;
  top: 100%;
  left: 0;
  margin-top: 4px;
  background: rgba(26, 34, 54, 0.95);
  border: 1px solid rgba(74, 85, 104, 0.3);
  border-radius: 4px;
  padding: 8px;
  max-height: 300px;
  overflow-y: auto;
  z-index: 1000;
  min-width: 200px;
  box-shadow: 0 4px 12px rgba(0, 0, 0, 0.3);
}

.multiselect-checkbox {
  display: flex;
  align-items: center;
  gap: 6px;
  padding: 4px 8px;
  font-size: 12px;
  color: #e0e0e0;
  cursor: pointer;
  border-radius: 3px;
  transition: background 0.15s;
}

.multiselect-checkbox:hover {
  background: rgba(41, 98, 255, 0.1);
}

.multiselect-checkbox input[type="checkbox"] {
  accent-color: #2962ff;
  margin: 0;
  cursor: pointer;
}

.multiselect-checkbox input[type="checkbox"]:checked + span {
  color: #2962ff;
  font-weight: 500;
}

/* 宏观指标选择器 */
.macro-selector {
  display: flex;
  align-items: center;
  gap: 6px;
}

.macro-selector label {
  font-size: 12px;
  color: #999;
  white-space: nowrap;
}

.pool-empty {
  font-size: 12px;
  color: #666;
  font-style: italic;
}

.control-spacer {
  flex: 1;
  min-width: 8px;
}

.field-selector {
  display: flex;
  align-items: center;
  gap: 6px;
}

.field-selector label {
  font-size: 12px;
  color: #999;
  white-space: nowrap;
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

/* 按钮样式 */
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
  min-height: 0;
}

.section {
  margin-bottom: 24px;
}

.section-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  margin-bottom: 12px;
}

.symbol-selector-inline {
  display: flex;
  align-items: center;
  gap: 6px;
}

.symbol-selector-inline label {
  font-size: 12px;
  color: #999;
  white-space: nowrap;
}

.section-title {
  margin: 0;
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
  height: 350px;
  overflow: hidden;
}

.chart-card.half {
  flex: 1;
  min-width: 0;
}

.chart-card.full {
  flex: 1 1 100%;
  min-width: 0;
  height: 400px;
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
