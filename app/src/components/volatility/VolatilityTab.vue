<template>
  <div class="volatility-tab">
    <!-- 顶部控制栏 -->
    <div class="control-bar">
      <div class="symbol-selector">
        <span
          v-for="(sym, idx) in state.symbols"
          :key="sym.symbol"
          class="symbol-tag"
        >
          {{ sym.symbol }}
          <button class="tag-close" @click="removeSymbol(idx)">×</button>
        </span>
        <input
          v-model="state.editingSymbol"
          placeholder="输入标的代码 (如 sz.000001)"
          class="symbol-input"
          @keyup.enter="addSymbolAndClear"
        />
        <button class="btn btn-small" @click="addSymbolAndClear">添加</button>
      </div>

      <div class="time-selector">
        <select v-model="state.quickRange" class="select-small" @change="setQuickRange">
          <option v-for="[label] in QUICK_RANGES" :key="label" :value="label">{{ label }}</option>
        </select>
        <div class="date-range-inline">
          <input type="date" :value="state.dateRange?.[0]" @input="updateStartDate($event)" class="date-input-small" placeholder="开始" />
          <span class="date-sep">至</span>
          <input type="date" :value="state.dateRange?.[1]" @input="updateEndDate($event)" class="date-input-small" placeholder="结束" />
        </div>
      </div>

      <div class="strategy-link">
        <select v-model="linkedStrategy" class="select-small" @change="onStrategyLink">
          <option value="">不联动</option>
          <option v-for="s in availableStrategies" :key="s.id" :value="s.id">{{ s.name }}</option>
        </select>
      </div>

      <button class="btn btn-primary btn-small" :disabled="loading" @click="runAnalysis">
        {{ loading ? '分析中...' : '开始分析' }}
      </button>
    </div>

    <!-- 分析结果 -->
    <div v-if="state.result" class="results">
      <!-- 单标的分析 -->
      <section v-if="state.symbols.length >= 1" class="section">
        <h3 class="section-title">单标的分析</h3>
        <div class="chart-grid">
          <div class="chart-card half">
            <ReturnDistributionChart :data="currentSingleResult" />
          </div>
          <div class="chart-card half">
            <RollingVolatilityChart :data="currentSingleResult" :windows="state.windows" />
          </div>
        </div>

        <div class="chart-grid">
          <div class="chart-card full">
            <PriceBandChart :data="currentSingleResult" />
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
          <div class="chart-card half">
            <VolatilityMetricsCard :data="currentSingleResult" />
          </div>
          <div class="chart-card half">
            <RiskMetricsCard :data="currentSingleResult" />
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
import { ref, computed, onMounted } from 'vue'
import { useVolatilityState, useVolatilityData } from './index'
import ReturnDistributionChart from './charts/ReturnDistributionChart.vue'
import RollingVolatilityChart from './charts/RollingVolatilityChart.vue'
import PriceBandChart from './charts/PriceBandChart.vue'
import VolatilityClusteringChart from './charts/VolatilityClusteringChart.vue'
import AutocorrelationChart from './charts/AutocorrelationChart.vue'
import VolatilityMetricsCard from './charts/VolatilityMetricsCard.vue'
import RiskMetricsCard from './charts/RiskMetricsCard.vue'
import CorrelationHeatmapChart from './charts/CorrelationHeatmapChart.vue'
import VolatilityComparisonChart from './charts/VolatilityComparisonChart.vue'
import CovarianceEigenChart from './charts/CovarianceEigenChart.vue'

const { state, QUICK_RANGES, addSymbol, removeSymbol, setQuickRange } = useVolatilityState()
const { loading, fetchVolatility } = useVolatilityData()

const linkedStrategy = ref('')
const availableStrategies = ref<{ id: string; name: string; symbols?: string[] }[]>([])

function addSymbolAndClear() {
  if (state.editingSymbol.trim()) {
    addSymbol(state.editingSymbol.trim())
    state.editingSymbol = ''
  }
}

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
  if (!state.dateRange) return
  
  const symbols = state.symbols.map(s => s.symbol)
  const [start_date, end_date] = state.dateRange
  
  const result = await fetchVolatility(symbols, start_date, end_date, state.windows)
  if (result) {
    state.result = result
    currentSymbolIndex.value = 0
  }
}

function onStrategyLink(strategyId: string) {
  const strategy = availableStrategies.value.find(s => s.id === strategyId)
  if (strategy?.symbols) {
    state.symbols = strategy.symbols.map(s => ({ symbol: s }))
    linkedStrategy.value = strategyId
  }
}

// 获取可用策略列表
onMounted(async () => {
  try {
    const resp = await fetch('/v0/strategy')
    if (resp.ok) {
      const strategies = await resp.json()
      availableStrategies.value = strategies.map((s: any) => ({
        id: s.id,
        name: s.name,
        symbols: s.symbols || []
      }))
    }
  } catch (e) {
    console.warn('[VolatilityTab] Failed to load strategies', e)
  }
})
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
  padding: 12px 16px;
  background: rgba(26, 34, 54, 0.8);
  border-bottom: 1px solid rgba(74, 85, 104, 0.3);
  flex-wrap: wrap;
}

.symbol-selector {
  display: flex;
  align-items: center;
  gap: 8px;
  flex-wrap: wrap;
}

.symbol-tag {
  display: inline-flex;
  align-items: center;
  gap: 4px;
  padding: 4px 8px;
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
  font-size: 16px;
  line-height: 1;
  cursor: pointer;
  padding: 0 2px;
  margin-left: 2px;
}

.tag-close:hover {
  color: #ef232a;
}

.symbol-input {
  padding: 4px 8px;
  background: rgba(26, 34, 54, 0.8);
  border: 1px solid rgba(74, 85, 104, 0.3);
  border-radius: 4px;
  color: #e0e0e0;
  font-size: 12px;
  outline: none;
  width: 180px;
}

.symbol-input:focus {
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
  min-height: 350px;
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
