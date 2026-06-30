<template>
  <div class="volatility-tab">
    <!-- 顶部控制栏 -->
    <AnalysisControlBar
      v-model:mode="mode"
      v-model:selectedStrategyId="selectedStrategyId"
      v-model:selectedMacroCountry="selectedMacroCountry"
      v-model:selectedMacroIndicator="selectedMacroIndicator"
      v-model:quickRange="state.quickRange"
      v-model:frequency="state.frequency"
      :strategy-options="strategyOptions"
      :available-securities="availableSecurities"
      :checked-symbols="checkedSymbols"
      :filtered-macro-options="filteredMacroOptions"
      :quick-ranges="QUICK_RANGES"
      :loading="loading"
      :can-analyze="canAnalyze"
      show-frequency
      @update:quickRange="setQuickRange(state.quickRange)"
      @update-date-range="updateDateRange"
      @toggle-symbol="toggleSymbol"
      @run-analysis="runAnalysis"
    >
      <template #extra-controls>
        <!-- 分析字段 (宏观模式隐藏) -->
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

        <!-- 包络带窗口 -->
        <div class="band-window-selector">
          <label>包络窗口:</label>
          <select v-model.number="state.bandWindow" class="select-small">
            <option :value="10">10天</option>
            <option :value="20">20天</option>
            <option :value="30">30天</option>
            <option :value="60">60天</option>
          </select>
        </div>
      </template>
    </AnalysisControlBar>

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

            <!-- 预测源选择（仅当任一预测可用时显示） -->
            <div v-if="currentSingleResult?.forecast_returns?.has_autocorrelation ||
                        currentSingleResult?.forecast_vol?.has_autocorrelation"
                 class="forecast-control-bar">
              <label class="forecast-label">预测:</label>
              <select v-model="forecastSourceMap[currentSymbolKey]" class="select-small">
                <option value="returns" v-if="currentSingleResult?.forecast_returns?.has_autocorrelation">
                  收益率 AR({{ currentSingleResult.forecast_returns.order_p }})
                </option>
                <option value="vol" v-if="currentSingleResult?.forecast_vol?.has_autocorrelation">
                  波动率 AR({{ currentSingleResult.forecast_vol.order_p }})
                </option>
              </select>
              <span class="forecast-note">{{ forecastNote }}</span>
            </div>

            <PriceBandChart
              :data="currentSingleResult"
              :forecast="currentForecast"
              :dates="state.result.dates"
            />
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

        <!-- 子标签切换 -->
        <div class="sub-tab-bar">
          <button
            :class="['sub-tab-btn', { active: activeMultiTab === 'correlation' }]"
            @click="activeMultiTab = 'correlation'"
          >协方差/相关性</button>
          <button
            :class="['sub-tab-btn', { active: activeMultiTab === 'timeseries' }]"
            @click="activeMultiTab = 'timeseries'"
          >时间序列分析</button>
        </div>

        <!-- 协方差/相关性 -->
        <template v-if="activeMultiTab === 'correlation'">
          <div class="chart-grid">
            <div class="chart-card full" :style="getCorrelationCardStyle()">
              <CorrelationHeatmapChart
                :symbols="state.result.symbols"
                :correlation-matrix="state.result.multi.correlation_matrix"
              />
              <div style="padding: 8px; font-size: 11px; color: #666;">
                标的数: {{ state.result.symbols?.length || 0 }} | 矩阵尺寸: {{ state.result.multi.correlation_matrix?.length || 0 }}
              </div>
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

          <!-- 多资产预测外推 -->
          <div v-if="state.result?.multi?.multi_forecast?.horizon" class="chart-grid">
            <div class="chart-card half">
              <CovarianceForecastChart :data="state.result.multi.multi_forecast" />
            </div>
            <div class="chart-card half">
              <VolatilityComparisonChart
                title="预测波动率对比"
                :symbols="state.result.multi.multi_forecast.symbols"
                :annual-volatility="state.result.multi.multi_forecast.forecast_volatilities"
              />
            </div>
          </div>
        </template>

        <!-- 时间序列分析 -->
        <template v-else-if="activeMultiTab === 'timeseries'">
          <div v-if="state.result?.multi?.time_series_analysis" class="chart-grid">
            <div class="chart-card full">
              <LeadLagHeatmapChart
                :data="state.result.multi.time_series_analysis.lead_lag"
              />
            </div>
          </div>

          <div v-if="state.result?.multi?.time_series_analysis" class="chart-grid">
            <div class="chart-card half">
              <GrangerTable
                :data="state.result.multi.time_series_analysis.granger_causality"
              />
            </div>
            <div class="chart-card half">
              <CointegrationTable
                :data="state.result.multi.time_series_analysis.cointegration"
              />
            </div>
          </div>

          <div v-else class="empty-mini">
            时间序列分析需要至少2个标的
          </div>
        </template>
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
import { ref, computed, watch } from 'vue'
import { useVolatilityState, useVolatilityData } from './index'
import { useStrategySecurities } from '../shared/composables/useStrategySecurities'
import { useMacroIndicators } from '../shared/composables/useMacroIndicators'
import AnalysisControlBar from '../shared/AnalysisControlBar.vue'
import ReturnDistributionChart from './charts/ReturnDistributionChart.vue'
import RollingVolatilityChart from './charts/RollingVolatilityChart.vue'
import PriceBandChart from './charts/PriceBandChart.vue'
import VolatilityClusteringChart from './charts/VolatilityClusteringChart.vue'
import AutocorrelationChart from './charts/AutocorrelationChart.vue'
import MetricsCard from './charts/MetricsCard.vue'
import CorrelationHeatmapChart from './charts/CorrelationHeatmapChart.vue'
import VolatilityComparisonChart from './charts/VolatilityComparisonChart.vue'
import CovarianceEigenChart from './charts/CovarianceEigenChart.vue'
import CovarianceForecastChart from './charts/CovarianceForecastChart.vue'
// 时间序列分析组件
import LeadLagHeatmapChart from './charts/LeadLagHeatmapChart.vue'
import GrangerTable from './charts/GrangerTable.vue'
import CointegrationTable from './charts/CointegrationTable.vue'

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

// 多标的子标签切换
const activeMultiTab = ref<'correlation' | 'timeseries'>('correlation')

// === 模式切换 ===
type AnalysisMode = 'strategy' | 'macro'
const mode = ref<AnalysisMode>('strategy')
const selectedMacroCountry = ref('china')
const selectedMacroIndicator = ref('')
const selectedSymbolForAdd = ref('')

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

// 监听策略 ID 变化，自动加载标的
watch(selectedStrategyId, (newId) => {
  if (newId) {
    loadSecuritiesForStrategy(newId)
  } else {
    availableSecurities.value = []
    checkedSymbols.value = new Set()
    state.symbols = []
  }
})

function updateDateRange(value: string, type: 'start' | 'end') {
  if (state.dateRange) {
    state.dateRange = type === 'start'
      ? [value, state.dateRange[1]]
      : [state.dateRange[0], value]
  }
}

// 当前选中的单标的结果（默认第一个）
const currentSymbolIndex = ref(0)
const currentSingleResult = computed(() => {
  if (!state.result?.single) return null
  const sym = state.symbols[currentSymbolIndex.value]?.symbol
  return sym ? state.result.single[sym] : null
})

// 相关性热力图卡片动态高度
function getCorrelationCardStyle(): Record<string, string> {
  const n = state.result?.symbols?.length || 0
  if (n <= 10) return {}
  const neededHeight = Math.max(350, n * 16 + 100)
  return { height: neededHeight + 'px' }
}

// === 每标的独立的预测源选择 ===
const forecastSourceMap = ref<Record<string, string>>({})

const currentSymbolKey = computed(() => {
  return state.symbols[currentSymbolIndex.value]?.symbol || ''
})

const currentForecast = computed(() => {
  const key = currentSymbolKey.value
  const sr = state.result?.single?.[key]
  if (!sr) return null
  const source = forecastSourceMap.value[key] || 'returns'
  return source === 'returns' ? sr.forecast_returns : sr.forecast_vol
})

const forecastNote = computed(() => {
  const fc = currentForecast.value
  return fc?.note || ''
})

// 当切换标的时，初始化默认预测源
watch(currentSymbolKey, (key) => {
  if (key && !forecastSourceMap.value[key]) {
    const sr = state.result?.single?.[key]
    forecastSourceMap.value[key] = sr?.forecast_returns?.has_autocorrelation ? 'returns' : 'vol'
  }
})

async function runAnalysis() {
  if (!state.dateRange || state.symbols.length === 0) return

  analysisLoading.value = true
  try {
    const symbols = state.symbols.map(s => s.symbol)
    const [start_date, end_date] = state.dateRange
    const field = mode.value === 'macro' ? 'value' : (state.field || 'close')

    const result = await fetchVolatility(symbols, start_date, end_date, state.windows, field, 'none', state.bandWindow, state.frequency)
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

/* 包络带窗口选择器（VolatilityTab 独有） */
.band-window-selector {
  display: flex;
  align-items: center;
  gap: 6px;
}

.band-window-selector label {
  font-size: 12px;
  color: #999;
  white-space: nowrap;
}

/* 字段选择器（VolatilityTab 独有） */
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

/* 预测控制栏 */
.forecast-control-bar {
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 6px 12px;
  margin-bottom: 8px;
  background: rgba(41, 98, 255, 0.08);
  border: 1px solid rgba(41, 98, 255, 0.2);
  border-radius: 4px;
}

.forecast-label {
  font-size: 12px;
  color: #999;
  white-space: nowrap;
}

.forecast-note {
  font-size: 11px;
  color: #666;
  font-style: italic;
  margin-left: 8px;
}

.strategy-link {
  display: flex;
  align-items: center;
  gap: 8px;
}

/* 通用 select-small 和按钮样式（被 extra-controls 插槽使用） */
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
  scrollbar-width: thin;
  scrollbar-color: rgba(255, 255, 255, 0.1) transparent;
}

.results::-webkit-scrollbar {
  width: 6px;
}

.results::-webkit-scrollbar-track {
  background: transparent;
}

.results::-webkit-scrollbar-thumb {
  background: rgba(255, 255, 255, 0.1);
  border-radius: 3px;
}

.results::-webkit-scrollbar-thumb:hover {
  background: rgba(255, 255, 255, 0.2);
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

/* 子标签切换栏 */
.sub-tab-bar {
  display: flex;
  gap: 4px;
  margin: 12px 0;
  padding: 4px;
  background: rgba(26, 34, 54, 0.5);
  border: 1px solid rgba(74, 85, 104, 0.2);
  border-radius: 6px;
}

.sub-tab-btn {
  padding: 6px 16px;
  background: transparent;
  border: none;
  border-radius: 4px;
  color: #999;
  font-size: 13px;
  cursor: pointer;
  transition: all 0.2s;
}

.sub-tab-btn:hover {
  color: #e0e0e0;
  background: rgba(41, 98, 255, 0.1);
}

.sub-tab-btn.active {
  background: #2962ff;
  color: #fff;
  font-weight: 500;
}

.empty-mini {
  text-align: center;
  color: #666;
  font-size: 13px;
  padding: 40px 20px;
  font-style: italic;
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
  height: 350px;
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
    min-height: 250px;
  }
}
</style>
