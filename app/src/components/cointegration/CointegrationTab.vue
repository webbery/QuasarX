<template>
  <div class="cointegration-tab">
    <!-- 控制栏 (与波动率面板一致) -->
    <AnalysisControlBar
      v-model:selectedStrategyId="selectedStrategyId"
      v-model:quickRange="state.quickRange.value"
      :strategy-options="strategyOptions"
      :available-securities="availableSecurities"
      :checked-symbols="checkedSymbols"
      :quick-ranges="QUICK_RANGES"
      :loading="loading"
      :can-analyze="canAnalyze"
      :show-mode-toggle="false"
      @update:quickRange="setQuickRange"
      @update-date-range="updateDateRange"
      @toggle-symbol="toggleSymbol"
      @run-analysis="onAnalyze"
    >
      <template #extra-controls>
        <div class="max-lag-selector">
          <label>最大滞后:</label>
          <input
            v-model.number="state.maxLag.value"
            type="number"
            min="1"
            max="40"
            class="num-input"
          />
        </div>
      </template>
    </AnalysisControlBar>

    <!-- 错误信息 -->
    <div v-if="state.error.value" class="error-msg">{{ state.error.value }}</div>

    <!-- 结果区 -->
    <div v-if="state.result.value" class="results-section">
      <!-- 1. 单位根检验 -->
      <div class="result-block">
        <UnitRootTable :data="state.result.value.unit_root" />
      </div>

      <!-- 2. 二元协整 -->
      <div class="result-block">
        <PairwiseEGTable :data="state.result.value.pairwise_eg" @select="onSelectEG" />
      </div>

      <!-- 3. 残差图 + OU 拟合 (选中某对时展示) -->
      <div v-if="selectedEG" class="result-block charts-row">
        <ResidualChart :data="selectedEG" :height="350" />
        <OUProcessChart :ou="selectedEG.ou" :residuals="selectedEG.residuals" :height="350" />
      </div>

      <!-- 4. Johansen (≥3 标的) -->
      <div v-if="state.hasJohansen.value" class="result-block">
        <JohansenTable :data="state.result.value.johansen!" />
      </div>

      <!-- 5. Granger 因果 -->
      <div class="result-block">
        <GrangerTable :data="state.result.value.granger" />
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, computed } from 'vue'
import { useCointegrationState } from './composables/useCointegrationState'
import { useStrategySecurities } from '../shared/composables/useStrategySecurities'
import AnalysisControlBar from '../shared/AnalysisControlBar.vue'
import UnitRootTable from './charts/UnitRootTable.vue'
import PairwiseEGTable from './charts/PairwiseEGTable.vue'
import ResidualChart from './charts/ResidualChart.vue'
import OUProcessChart from './charts/OUProcessChart.vue'
import JohansenTable from './charts/JohansenTable.vue'
import GrangerTable from './charts/GrangerTable.vue'

const state = useCointegrationState()
const { QUICK_RANGES, setQuickRange, updateDateRange } = state
const {
  strategyOptions,
  selectedStrategyId,
  availableSecurities,
  checkedSymbols,
  loading: securitiesLoading,
  toggleSymbol,
} = useStrategySecurities({ defaultCheckAll: false })

const loading = computed(() => securitiesLoading.value || state.loading.value)

// 至少选 2 个标的才可分析（与波动率面板行为对齐：勾选非空即可）
const canAnalyze = computed(() => {
  if (loading.value) return false
  return checkedSymbols.value.size >= 2
})

const selectedEGIndex = ref(-1)

const selectedEG = computed(() => {
  if (selectedEGIndex.value < 0 || !state.result.value) return null
  return state.result.value.pairwise_eg[selectedEGIndex.value] ?? null
})

function onAnalyze() {
  state.symbols.value = Array.from(checkedSymbols.value)
  selectedEGIndex.value = -1
  state.analyze()
}

function onSelectEG(idx: number) {
  selectedEGIndex.value = idx
}
</script>

<style scoped>
.cointegration-tab {
  display: flex;
  flex-direction: column;
  height: 100%;
  background: #1a2236;
  color: #e0e0e0;
}

.max-lag-selector {
  display: flex;
  align-items: center;
  gap: 6px;
}

.max-lag-selector label {
  font-size: 12px;
  color: #999;
  white-space: nowrap;
}

.num-input {
  width: 60px;
  background: rgba(26, 34, 54, 0.8);
  border: 1px solid rgba(74, 85, 104, 0.3);
  border-radius: 4px;
  padding: 4px 8px;
  color: #e0e0e0;
  font-size: 12px;
  outline: none;
  text-align: center;
}

.num-input:focus {
  border-color: rgba(41, 98, 255, 0.5);
}

.error-msg {
  color: #f44336;
  font-size: 12px;
  padding: 8px 16px;
  background: rgba(244, 67, 54, 0.08);
  border-bottom: 1px solid rgba(244, 67, 54, 0.2);
}

.results-section {
  flex: 1;
  overflow-y: auto;
  padding: 12px;
  min-height: 0;
}

.result-block {
  margin-bottom: 20px;
  padding: 12px;
  background: rgba(26, 34, 54, 0.5);
  border: 1px solid rgba(74, 85, 104, 0.2);
  border-radius: 8px;
}

.charts-row {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 12px;
}
</style>