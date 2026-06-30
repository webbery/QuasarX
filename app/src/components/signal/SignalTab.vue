<template>
  <div class="signal-tab">
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
      </template>
    </AnalysisControlBar>

    <!-- 分析结果 -->
    <div v-if="result" class="results">
      <section class="section">
        <h3 class="section-title">EMD 分解</h3>
        <div class="chart-grid">
          <div class="chart-card full">
            <IMF3DChart :data="result" />
          </div>
        </div>

        <div class="chart-grid">
          <div class="chart-card half">
            <IMFEnergyChart :data="result" />
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
import { useMacroIndicators } from '../shared/composables/useMacroIndicators'
import AnalysisControlBar from '../shared/AnalysisControlBar.vue'
import IMF3DChart from './charts/IMF3DChart.vue'
import IMFEnergyChart from './charts/IMFEnergyChart.vue'

const { state, result, QUICK_RANGES, removeSymbol, setQuickRange } = useSignalState()
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
const { macroOptionsByCountry } = useMacroIndicators()

// === 模式切换 ===
type AnalysisMode = 'strategy' | 'macro'
const mode = ref<AnalysisMode>('strategy')
const selectedMacroCountry = ref('china')
const selectedMacroIndicator = ref('')

const filteredMacroOptions = computed(() =>
  macroOptionsByCountry.value[selectedMacroCountry.value] || []
)

const analysisLoading = ref(false)
const loading = computed(() => securitiesLoading.value || analysisLoading.value)

const canAnalyze = computed(() => {
  if (loading.value) return false
  if (mode.value === 'macro') {
    return !!selectedMacroIndicator.value
  }
  return state.symbols.length > 0
})

function switchMode(newMode: AnalysisMode) {
  mode.value = newMode
  if (newMode === 'strategy') {
    selectedMacroIndicator.value = ''
    state.symbols = []
  } else {
    selectedStrategyId.value = ''
    availableSecurities.value = []
    checkedSymbols.value = new Set()
    state.symbols = []
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
  }
}

// 监听勾选变化，同步到 state.symbols
watch(checkedSymbols, (next) => {
  if (mode.value !== 'strategy') return
  // 直接重新构建 symbols 数组，避免 push + filter 导致的中间状态
  state.symbols = Array.from(next).map(code => ({ symbol: code }))
}, { deep: false })

// 监听宏观指标选择变化
watch([selectedMacroCountry, selectedMacroIndicator], ([country, indicator]) => {
  if (mode.value !== 'macro' || !indicator) return
  state.symbols = [{ symbol: `${country}/${indicator}` }]
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

async function runAnalysis() {
  if (!state.dateRange || state.symbols.length === 0) return

  analysisLoading.value = true
  try {
    // 在 await 之前捕获数据，避免异步返回后 state 已被修改
    const symbols = [...state.symbols.map(s => s.symbol)]
    const [start_date, end_date] = [...state.dateRange]
    const field = mode.value === 'macro' ? 'value' : (state.field || 'close')
    const method = state.method
    const numImfs = state.numImfs

    const result_data = await fetchSignal(symbols, start_date, end_date, field, method, numImfs)
    if (result_data) {
      // 使用 shallowRef，避免深度响应式代理大型数组
      result.value = result_data
    }
  } catch (e) {
    console.error('[SignalTab] Analysis error:', e)
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

/* 字段选择器（SignalTab 独有） */
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

/* 方法选择器（SignalTab 独有） */
.method-selector {
  display: flex;
  align-items: center;
  gap: 6px;
}

.method-selector label {
  font-size: 12px;
  color: #999;
  white-space: nowrap;
}

/* IMF 数量选择器（SignalTab 独有） */
.imf-count-selector {
  display: flex;
  align-items: center;
  gap: 6px;
}

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

/* 通用 select-small（被 extra-controls 插槽使用） */
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
  height: 500px;
}

.chart-card.half {
  flex: 1;
  height: 400px;
}

.chart-card.full {
  flex: 1;
  min-height: 600px;
  height: 600px;
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
