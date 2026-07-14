<template>
  <div class="fundamental-tab">
    <!-- 顶部控制栏 -->
    <div class="control-bar">
      <!-- 标的选择 -->
      <div class="control-group">
        <label>标的:</label>
        <StrategySelector
          :selected-strategy-id="selectedStrategyId"
          :strategy-options="strategyOptions"
          :available-securities="availableSecurities"
          :checked-symbols="checkedSymbols"
          :loading="state.loading"
          @update:selectedStrategyId="selectedStrategyId = $event"
          @toggle-symbol="toggleSymbol"
        />
      </div>

      <div class="control-spacer" />

      <!-- 散点图 Y 轴天数 -->
      <div class="control-group">
        <label>未来N日:</label>
        <select v-model.number="state.scatterYDays" class="select-small">
          <option :value="10">10日</option>
          <option :value="20">20日</option>
          <option :value="30">30日</option>
          <option :value="60">60日</option>
        </select>
      </div>

      <!-- 时间范围 -->
      <DateRangeSelector
        v-model:quickRange="state.quickRange"
        v-model:dateRange="state.dateRange"
        :quick-ranges="QUICK_RANGES"
        @update:quickRange="setQuickRange($event)"
        @update:dateRange="state.dateRange = $event"
      />

      <button class="btn btn-primary btn-sm" :disabled="state.loading || !canAnalyze" @click="runAnalysis">
        {{ state.loading ? '加载中...' : '分析' }}
      </button>
    </div>

    <!-- 图表配置 checkbox -->
    <div class="chart-config">
      <label v-for="item in chartItems" :key="item.id" class="chart-checkbox">
        <input type="checkbox" v-model="item.enabled" />
        <span>{{ item.label }}</span>
      </label>
    </div>

    <!-- 图表网格 -->
    <div v-if="state.aligned" class="chart-grid">
      <!-- 第一行: 2列 -->
      <div class="grid-row">
        <div v-if="chartEnabled('priceValuation')" class="chart-card half">
          <PriceValuationChart :data="state.aligned" />
        </div>
        <div v-if="chartEnabled('profitability')" class="chart-card half">
          <ProfitabilityChart :data="state.data?.profit?.data || []" />
        </div>
      </div>

      <!-- 第二行: 2列 -->
      <div class="grid-row">
        <div v-if="chartEnabled('valuationBand')" class="chart-card half">
          <ValuationBandChart :data="state.aligned" :bands="state.valuationBands" />
        </div>
        <div v-if="chartEnabled('heatmap')" class="chart-card half">
          <FundamentalHeatmapChart
            :profitData="state.data?.profit?.data || []"
            :growthData="state.data?.growth?.data || []"
            :balanceData="state.data?.balance?.data || []"
            :cashflowData="state.data?.cashflow?.data || []"
            :metrics="state.heatmapMetrics"
          />
        </div>
      </div>

      <!-- 第三行: 全宽 -->
      <div v-if="chartEnabled('scatter')" class="grid-row">
        <div class="chart-card full">
          <CorrelationScatterChart
            :aligned="state.aligned"
            :prices="state.data?.prices || []"
            :yDays="state.scatterYDays"
          />
        </div>
      </div>
    </div>

    <!-- 空状态 -->
    <div v-else-if="!state.loading" class="empty-state">
      <p>选择标的并点击"分析"以查看基本面数据可视化</p>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, computed, reactive } from 'vue'
import { useFundamentalState } from './composables/useFundamentalState'
import { useFundamentalData } from './composables/useFundamentalData'
import { useStrategySecurities } from '../shared/composables/useStrategySecurities'
import StrategySelector from '../shared/StrategySelector.vue'
import DateRangeSelector from '../shared/DateRangeSelector.vue'
import PriceValuationChart from './charts/PriceValuationChart.vue'
import ProfitabilityChart from './charts/ProfitabilityChart.vue'
import ValuationBandChart from './charts/ValuationBandChart.vue'
import FundamentalHeatmapChart from './charts/FundamentalHeatmapChart.vue'
import CorrelationScatterChart from './charts/CorrelationScatterChart.vue'

const { state, QUICK_RANGES, setQuickRange } = useFundamentalState()
const { loading, fetchFundamental } = useFundamentalData()
const { strategyOptions, availableSecurities, checkedSymbols, toggleSymbol } = useStrategySecurities()

const selectedStrategyId = ref('')

// 图表可见性配置
const chartItems = reactive([
  { id: 'priceValuation', label: '股价 & PE', enabled: true },
  { id: 'profitability', label: '盈利能力', enabled: true },
  { id: 'valuationBand', label: '估值带', enabled: true },
  { id: 'heatmap', label: '热力图', enabled: true },
  { id: 'scatter', label: '散点图', enabled: true },
])

function chartEnabled(id: string): boolean {
  return chartItems.find(item => item.id === id)?.enabled ?? false
}

const canAnalyze = computed(() => {
  return checkedSymbols.value.size > 0
})

async function runAnalysis() {
  if (checkedSymbols.value.size === 0) return

  // 取第一个标的
  const symbolCode = Array.from(checkedSymbols.value)[0]
  state.symbol = symbolCode

  const result = await fetchFundamental(
    symbolCode,
    state.dateRange[0],
    state.dateRange[1],
  )

  if (result) {
    state.data = result.data
    state.aligned = result.aligned
  }
}
</script>

<style scoped>
.fundamental-tab {
  display: flex;
  flex-direction: column;
  height: 100%;
  overflow-y: auto;
}

.control-bar {
  display: flex;
  align-items: center;
  gap: 12px;
  padding: 8px 12px;
  border-bottom: 1px solid #333;
  flex-wrap: wrap;
}

.control-group {
  display: flex;
  align-items: center;
  gap: 6px;
}

.control-group label {
  color: #a0aec0;
  font-size: 12px;
  white-space: nowrap;
}

.control-spacer {
  flex: 1;
}

.select-small {
  background: #1a1a2e;
  color: #e0e0e0;
  border: 1px solid #444;
  border-radius: 3px;
  padding: 2px 6px;
  font-size: 12px;
}

.chart-config {
  display: flex;
  gap: 16px;
  padding: 6px 12px;
  border-bottom: 1px solid #333;
  background: rgba(26, 26, 46, 0.5);
}

.chart-checkbox {
  display: flex;
  align-items: center;
  gap: 4px;
  color: #a0aec0;
  font-size: 12px;
  cursor: pointer;
}

.chart-checkbox input[type="checkbox"] {
  accent-color: #2962ff;
}

.chart-grid {
  flex: 1;
  padding: 8px;
}

.grid-row {
  display: flex;
  gap: 8px;
  margin-bottom: 8px;
}

.chart-card {
  background: #1a1a2e;
  border: 1px solid #333;
  border-radius: 4px;
  min-height: 300px;
}

.chart-card.half {
  flex: 1;
}

.chart-card.full {
  width: 100%;
}

.empty-state {
  flex: 1;
  display: flex;
  align-items: center;
  justify-content: center;
  color: #666;
  font-size: 14px;
}

.btn {
  padding: 4px 12px;
  border: none;
  border-radius: 3px;
  cursor: pointer;
  font-size: 12px;
}

.btn-primary {
  background: #2962ff;
  color: white;
}

.btn-primary:disabled {
  background: #444;
  cursor: not-allowed;
}

.btn-sm {
  padding: 3px 10px;
}
</style>
