<template>
  <div class="volatility-tab">
    <!-- 顶部控制栏 -->
    <div class="control-bar">
      <div class="symbol-selector">
        <el-tag
          v-for="(sym, idx) in state.symbols"
          :key="sym.symbol"
          closable
          @close="removeSymbol(idx)"
          class="symbol-tag"
        >
          {{ sym.symbol }}
        </el-tag>
        <el-input
          v-model="state.editingSymbol"
          placeholder="输入标的代码 (如 sz.000001)"
          size="small"
          style="width: 200px"
          @keyup.enter="addSymbolAndClear"
        />
        <el-button size="small" @click="addSymbolAndClear">添加</el-button>
      </div>

      <div class="time-selector">
        <el-select v-model="state.quickRange" size="small" style="width: 100px" @change="setQuickRange">
          <el-option v-for="[label] in QUICK_RANGES" :key="label" :label="label" :value="label" />
        </el-select>
        <el-date-picker
          v-model="state.dateRange"
          type="daterange"
          range-separator="至"
          start-placeholder="开始"
          end-placeholder="结束"
          size="small"
          style="width: 260px"
          value-format="YYYY-MM-DD"
        />
      </div>

      <div class="strategy-link">
        <el-select v-model="linkedStrategy" placeholder="策略联动" size="small" style="width: 180px" @change="onStrategyLink">
          <el-option label="不联动" value="" />
          <el-option v-for="s in availableStrategies" :key="s.id" :label="s.name" :value="s.id" />
        </el-select>
      </div>

      <el-button type="primary" size="small" :loading="loading" @click="runAnalysis">
        开始分析
      </el-button>
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
      <el-empty description="请添加标的并点击「开始分析」" />
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
  margin: 0;
}

.time-selector {
  display: flex;
  align-items: center;
  gap: 8px;
}

.strategy-link {
  display: flex;
  align-items: center;
  gap: 8px;
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

@media (max-width: 1200px) {
  .chart-grid {
    flex-direction: column;
  }
  .chart-card.half {
    min-height: 300px;
  }
}
</style>
