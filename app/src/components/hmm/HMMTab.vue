<template>
  <div class="hmm-tab">
    <!-- 顶部控制栏 -->
    <AnalysisControlBar
      mode="strategy"
      :show-mode-toggle="false"
      v-model:selectedStrategyId="selectedStrategyId"
      :quick-range="quickRange"
      :frequency="frequency"
      :date-range="dateRange"
      :strategy-options="strategyOptions"
      :available-securities="availableSecurities"
      :checked-symbols="checkedSymbols"
      :quick-ranges="QUICK_RANGES"
      :loading="loading"
      :can-analyze="canAnalyze"
      run-label="开始分析"
      show-frequency
      @update:quickRange="setQuickRange($event)"
      @update:frequency="setFrequency($event)"
      @update-date-range="updateDateRange"
      @toggle-symbol="toggleSymbol"
      @run-analysis="runAnalysis"
    >
      <template #extra-controls>
        <!-- 状态数 -->
        <div class="param-control">
          <label>状态数:</label>
          <input
            v-model.number="config.n_states"
            type="number"
            min="2"
            max="8"
            class="number-input"
          />
        </div>

        <!-- 特征选择 -->
        <div class="field-selector">
          <label>特征:</label>
          <select :value="feature" class="select-small" @change="setFeature(($event.target as HTMLSelectElement).value)">
            <option v-for="opt in HMM_FEATURE_OPTIONS" :key="opt.value" :value="opt.value">{{ opt.label }}</option>
          </select>
        </div>
      </template>
    </AnalysisControlBar>

    <div v-if="!selectedStrategyId" class="stage-empty stage-empty-hero">
      <div class="stage-icon">00</div>
      <div>
        <h3>选择一个含 HMM 节点的策略</h3>
        <p>HMM 隐马尔可夫分析用于市场状态识别（牛/熊/震荡）。</p>
      </div>
    </div>

    <template v-else>
      <div class="compact-status">
        <span class="node-status"><i></i>HMM</span>
        <span class="status-sep">·</span>
        <span>{{ selectedStrategyName }}</span>
        <span class="status-sep">·</span>
        <span>{{ checkedSymbols.size }} 个标的</span>
        <span class="status-sep">·</span>
        <span>{{ dateRange?.[0] || '—' }} → {{ dateRange?.[1] || '—' }}</span>
      </div>

      <!-- 分析结果 -->
      <div v-if="result" class="results">
        <section class="section">
          <h3 class="section-title">状态识别结果</h3>
          <div class="chart-grid">
            <div class="chart-card full">
              <StateSequenceChart :data="result" />
            </div>
          </div>

          <div class="chart-grid">
            <div class="chart-card half">
              <StateProbabilityChart :data="result" />
            </div>
            <div class="chart-card half">
              <TransitionMatrixChart :data="result" />
            </div>
          </div>
        </section>

        <section class="section">
          <h3 class="section-title">模型诊断</h3>
          <div class="chart-grid">
            <div class="chart-card half">
              <DurationChart :data="result" />
            </div>
            <div class="chart-card half">
              <LogLikelihoodChart :data="result" />
            </div>
          </div>

          <div class="chart-grid">
            <div class="chart-card full">
              <StateRadarChart :data="result" :features="result.feature_names" />
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
    </template>
  </div>
</template>

<script setup lang="ts">
import { computed, watch } from 'vue'
import { useHMMState, HMM_FEATURE_OPTIONS } from './composables/useHMMState'
import { useHMMData } from './composables/useHMMData'
import { useStrategySecurities } from '@/components/shared/composables/useStrategySecurities'
import AnalysisControlBar from '@/components/shared/AnalysisControlBar.vue'
import StateSequenceChart from './charts/StateSequenceChart.vue'
import StateProbabilityChart from './charts/StateProbabilityChart.vue'
import TransitionMatrixChart from './charts/TransitionMatrixChart.vue'
import DurationChart from './charts/DurationChart.vue'
import LogLikelihoodChart from './charts/LogLikelihoodChart.vue'
import StateRadarChart from './charts/StateRadarChart.vue'

const {
  config,
  result,
  loading,
  selectedStrategyId,
  quickRange,
  dateRange,
  frequency,
  feature,
  QUICK_RANGES,
  setQuickRange,
  setFeature,
  reset
} = useHMMState()

const { fetchHMMAnalysis } = useHMMData()

const {
  strategyOptions,
  availableSecurities,
  checkedSymbols,
  loadSecuritiesForStrategy,
  toggleSymbol,
} = useStrategySecurities()

const canAnalyze = computed(() => {
  if (loading.value) return false
  if (!selectedStrategyId.value || !dateRange.value) return false
  return checkedSymbols.value.size > 0
})

function setFrequency(value: string) {
  frequency.value = value
}

function updateDateRange(value: string, type: 'start' | 'end') {
  if (dateRange.value) {
    dateRange.value = type === 'start'
      ? [value, dateRange.value[1]]
      : [dateRange.value[0], value]
  }
}

const selectedStrategyName = computed(() => {
  return strategyOptions.value.find(opt => opt.id === selectedStrategyId.value)?.name || '—'
})

watch(selectedStrategyId, (newId) => {
  if (newId) {
    loadSecuritiesForStrategy(newId)
  } else {
    reset()
  }
})

async function runAnalysis() {
  if (checkedSymbols.value.size === 0 || !dateRange.value || !selectedStrategyId.value) return

  loading.value = true
  try {
    const [startDate, endDate] = dateRange.value
    const resultData = await fetchHMMAnalysis(
      Array.from(checkedSymbols.value),
      selectedStrategyId.value,
      startDate,
      endDate,
      frequency.value,
      config.value
    )
    if (resultData) {
      result.value = resultData
    }
  } catch (e) {
    console.error('[HMMTab] Analysis error:', e)
  } finally {
    loading.value = false
  }
}
</script>

<style scoped>
.hmm-tab {
  display: flex;
  flex-direction: column;
  height: 100%;
  background: #1a2236;
  color: #e0e0e0;
}

.param-control {
  display: flex;
  align-items: center;
  gap: 6px;
}

.param-control label {
  font-size: 12px;
  color: #999;
  white-space: nowrap;
}

.number-input {
  padding: 4px 8px;
  background: rgba(26, 34, 54, 0.8);
  border: 1px solid rgba(74, 85, 104, 0.3);
  border-radius: 4px;
  color: #e0e0e0;
  font-size: 12px;
  outline: none;
  width: 60px;
  text-align: center;
}

.number-input:focus {
  border-color: rgba(41, 98, 255, 0.5);
}

/* 与 ML 面板「字段:」下拉框保持一致 */
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

.compact-status {
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 8px 16px;
  font-size: 12px;
  color: #94a3b8;
  border-bottom: 1px solid rgba(74, 85, 104, 0.2);
  flex-wrap: wrap;
}

.status-sep {
  color: #4a5568;
}

.node-status {
  display: inline-flex;
  align-items: center;
  gap: 5px;
  color: #34d399;
  font-size: 11px;
  font-weight: 600;
}

.node-status i {
  width: 6px;
  height: 6px;
  border-radius: 50%;
  background: #34d399;
  box-shadow: 0 0 4px rgba(38, 166, 91, 0.5);
  display: inline-block;
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
  height: 400px;
}

.chart-card.half {
  flex: 1;
}

.chart-card.full {
  flex: 1;
  min-height: 400px;
  height: 500px;
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

.stage-empty {
  display: flex;
  gap: 14px;
  margin: 8px;
  padding: 20px 24px;
  background: rgba(15, 25, 41, 0.6);
  border: 1px dashed rgba(74, 85, 104, 0.5);
  border-radius: 8px;
  color: #94a3b8;
}

.stage-empty-hero {
  margin: 24px 16px;
  padding: 32px 28px;
}

.stage-icon {
  font-size: 32px;
  line-height: 1;
  flex-shrink: 0;
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
