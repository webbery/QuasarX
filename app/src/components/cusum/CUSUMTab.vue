<template>
  <div class="cusum-tab">
    <!-- 头部控制区 -->
    <header class="cusum-header">
      <!-- 标的池：使用 StrategySelector 组件 -->
      <StrategySelector
        :selected-strategy-id="selectedStrategyId"
        :strategy-options="strategyOptions"
        :available-securities="availableSecurities"
        :checked-symbols="checkedSymbols"
        :loading="isLoading"
        @update:selectedStrategyId="selectedStrategyId = $event"
        @toggle-symbol="toggleSymbol"
      />

      <div class="control-group">
        <DateRangeSelector
          v-model:quickRange="quickRange"
          v-model:frequency="frequency"
          :quick-ranges="QUICK_RANGES"
          :date-range="dateRange"
          :show-frequency="false"
          @update-date-range="updateDateRange"
        />
      </div>

      <div class="control-group">
        <label>CUSUM 参数：</label>
        <span class="param-label">λ</span>
        <input type="number" v-model.number="lambda" class="select-small" step="0.1" min="0.1" max="2.0" />
        <span class="param-label">h</span>
        <input type="number" v-model.number="threshold" class="select-small" step="0.5" min="1.0" max="10.0" />
        <span class="param-label">min_obs</span>
        <input type="number" v-model.number="minObs" class="select-small" step="5" min="10" max="100" />
      </div>

      <div class="control-group">
        <label>检测模式：</label>
        <label class="checkbox-label">
          <input type="checkbox" v-model="modes.mean" />
          <span>均值漂移</span>
        </label>
        <label class="checkbox-label">
          <input type="checkbox" v-model="modes.variance" />
          <span>方差漂移</span>
        </label>
        <label class="checkbox-label">
          <input type="checkbox" v-model="modes.correlation" />
          <span>相关性变化</span>
        </label>
      </div>

      <div class="control-group actions">
        <button class="btn btn-primary btn-small" @click="runAnalysis" :disabled="isLoading || checkedSymbols.size === 0">
          {{ isLoading ? '分析中...' : '开始分析' }}
        </button>
      </div>
    </header>

    <!-- 加载状态 -->
    <div v-if="isLoading" class="loading-state">
      <div class="spinner"></div>
      <span>CUSUM 检测中，请稍候...</span>
    </div>

    <!-- 空状态 -->
    <div v-else-if="!hasResult" class="empty-state">
      <div class="empty-icon">🔍</div>
      <div class="empty-text">配置参数后点击"开始分析"</div>
      <div class="empty-hint">支持检测均值漂移、方差漂移、相关性结构变化</div>
    </div>

    <!-- 结果展示区 -->
    <div v-if="hasResult" class="results-container">
      <!-- 1. 均值漂移检测 -->
      <div class="result-section">
        <div class="section-header">
          <h3>📊 均值漂移检测 (Mean Shift)</h3>
          <span class="status-badge normal">
            {{ result.mean_cusum ? result.mean_cusum.length : 0 }} 只标的
          </span>
        </div>
        <MeanShiftChart
          v-if="result.mean_cusum && result.mean_cusum.length"
          :results="result.mean_cusum"
          :dates="result.dates"
        />
      </div>

      <!-- 2. 方差漂移检测 -->
      <div class="result-section">
        <div class="section-header">
          <h3>📈 方差漂移检测 (Variance Shift)</h3>
          <span class="status-badge normal">
            {{ result.variance_cusum ? result.variance_cusum.length : 0 }} 只标的
          </span>
        </div>
        <VarianceShiftChart
          v-if="result.variance_cusum && result.variance_cusum.length"
          :results="result.variance_cusum"
          :dates="result.dates"
        />
      </div>

      <!-- 3. 相关性结构变化 -->
      <div v-if="modes.correlation && result.correlation" class="result-section">
        <div class="section-header">
          <h3>🔗 相关性结构变化 (Correlation Change)</h3>
          <span :class="['status-badge', result.correlation.change_points.length > 0 ? 'warning' : 'normal']">
            {{ result.correlation.change_points.length > 0 ? `⚠ ${result.correlation.change_points.length} 次变点` : '✓ 正常' }}
          </span>
        </div>
        <CorrelationChart
          :rolling-avg="result.correlation.rolling_avg"
          :matrix-before="result.correlation.matrix_before"
          :matrix-after="result.correlation.matrix_after"
          :change-points="result.correlation.change_points"
          :dates="result.dates"
          :symbols="result.correlation.symbols"
        />
      </div>

      <!-- 4. 变点时间轴 -->
      <div v-if="result.timeline && result.timeline.length > 0" class="result-section">
        <div class="section-header">
          <h3>⏱️ 变点时间轴 (Change Point Timeline)</h3>
        </div>
        <TimelineChart
          :events="result.timeline"
          :total-days="result.dates.length"
          :dates="result.dates"
        />
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, reactive, computed, watch } from 'vue'
import axios from 'axios'
import StrategySelector from '../shared/StrategySelector.vue'
import DateRangeSelector from '../shared/DateRangeSelector.vue'
import MeanShiftChart from './charts/MeanShiftChart.vue'
import VarianceShiftChart from './charts/VarianceShiftChart.vue'
import CorrelationChart from './charts/CorrelationChart.vue'
import TimelineChart from './charts/TimelineChart.vue'
import { useStrategySecurities } from '../shared/composables/useStrategySecurities'

interface Security {
  code: string
  name?: string
}

// === 标的池状态（使用 StrategySelector + useStrategySecurities） ===
const {
  strategyOptions,
  selectedStrategyId,
  availableSecurities,
  checkedSymbols,
  loading: securitiesLoading,
  loadSecuritiesForStrategy,
  toggleSymbol,
} = useStrategySecurities()

// === 快速范围默认值 ===
const QUICK_RANGES: [string, () => [string, string]][] = [
  ['近1月', () => {
    const end = new Date()
    const start = new Date()
    start.setMonth(start.getMonth() - 1)
    return [start.toISOString().slice(0, 10), end.toISOString().slice(0, 10)]
  }],
  ['近3月', () => {
    const end = new Date()
    const start = new Date()
    start.setMonth(start.getMonth() - 3)
    return [start.toISOString().slice(0, 10), end.toISOString().slice(0, 10)]
  }],
  ['近6月', () => {
    const end = new Date()
    const start = new Date()
    start.setMonth(start.getMonth() - 6)
    return [start.toISOString().slice(0, 10), end.toISOString().slice(0, 10)]
  }],
  ['近1年', () => {
    const end = new Date()
    const start = new Date()
    start.setFullYear(start.getFullYear() - 1)
    return [start.toISOString().slice(0, 10), end.toISOString().slice(0, 10)]
  }],
  ['近3年', () => {
    const end = new Date()
    const start = new Date()
    start.setFullYear(start.getFullYear() - 3)
    return [start.toISOString().slice(0, 10), end.toISOString().slice(0, 10)]
  }],
]

// === 表单状态 ===
const quickRange = ref('近1年')
const frequency = ref('1d')
const dateRange = ref<[string, string] | null>(null)

// 初始化日期范围
function initDateRangeFromQuickRange() {
  const range = QUICK_RANGES.find(r => r[0] === quickRange.value)
  if (range) {
    dateRange.value = range[1]()
  }
}

// 初始化
initDateRangeFromQuickRange()

const lambda = ref(0.5)
const threshold = ref(4.0)
const minObs = ref(30)
const modes = reactive({
  mean: true,
  variance: true,
  correlation: true,
})

const loading = ref(false)
const hasResult = ref(false)
const result = ref<any>({})

// 合并加载状态
const isLoading = computed(() => securitiesLoading.value || loading.value)

// 监听策略 ID 变化，自动加载标的
watch(selectedStrategyId, (newId) => {
  if (newId) {
    loadSecuritiesForStrategy(newId)
  } else {
    availableSecurities.value = []
    checkedSymbols.value = new Set()
  }
})

// 监听 quickRange 变化，更新日期范围
watch(quickRange, () => {
  initDateRangeFromQuickRange()
})

function updateDateRange(value: string, type: 'start' | 'end') {
  if (dateRange.value) {
    dateRange.value = type === 'start'
      ? [value, dateRange.value[1]]
      : [dateRange.value[0], value]
  }
}

// === 方法 ===

async function runAnalysis() {
  if (checkedSymbols.value.size === 0) {
    alert('请先选择标的池')
    return
  }

  if (!dateRange.value) {
    alert('请选择时间范围')
    return
  }

  loading.value = true
  hasResult.value = false

  try {
    const activeModes = []
    if (modes.mean) activeModes.push('mean')
    if (modes.variance) activeModes.push('variance')
    if (modes.correlation) activeModes.push('correlation')

    const requestBody = {
      symbols: Array.from(checkedSymbols.value),
      start: dateRange.value[0],
      end: dateRange.value[1],
      lambda: lambda.value,
      threshold_multiplier: threshold.value,
      min_obs: minObs.value,
      modes: activeModes,
      freq: '1d',
    }

    console.log('[CUSUMTab] Sending request:', requestBody)

    const response = await axios.post('/v0/analysis/cusum', requestBody)

    console.log('[CUSUMTab] Response status:', response.status)
    console.log('[CUSUMTab] Response data:', response.data)

    result.value = response.data
    hasResult.value = true
  } catch (err: any) {
    console.error('[CUSUMTab] Analysis error:', err)
    alert(`分析失败：${err.message}`)
  } finally {
    loading.value = false
  }
}

function resetForm() {
  selectedStrategyId.value = ''
  availableSecurities.value = []
  checkedSymbols.value = new Set()
  quickRange.value = '近1年'
  frequency.value = '1d'
  initDateRangeFromQuickRange()
  lambda.value = 0.5
  threshold.value = 4.0
  minObs.value = 30
  modes.mean = true
  modes.variance = true
  modes.correlation = true
  hasResult.value = false
  result.value = {}
}
</script>

<style scoped>
.cusum-tab {
  height: 100%;
  display: flex;
  flex-direction: column;
  background: #1a2236;
  color: #e0e0e0;
  overflow: hidden;
}

/* === 头部控制区 === */

.cusum-header {
  display: flex;
  flex-wrap: wrap;
  gap: 12px;
  padding: 10px 16px;
  background: rgba(26, 34, 54, 0.8);
  border-bottom: 1px solid rgba(74, 85, 104, 0.3);
  align-items: center;
  min-height: 52px;
}

.control-group {
  display: flex;
  align-items: center;
  gap: 12px;
}

.control-group label {
  font-size: 12px;
  color: #999;
  white-space: nowrap;
}

.param-label {
  font-size: 12px;
  color: #999;
  font-weight: 600;
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
  width: 60px;
  text-align: center;
}

.select-small:focus {
  border-color: rgba(41, 98, 255, 0.5);
}

.checkbox-label {
  display: flex;
  align-items: center;
  gap: 4px;
  font-size: 12px;
  color: #999;
  cursor: pointer;
  white-space: nowrap;
}

.checkbox-label input[type="checkbox"] {
  accent-color: #2962ff;
  margin: 0;
  cursor: pointer;
}

.checkbox-label input[type="checkbox"]:checked + span {
  color: #2962ff;
  font-weight: 500;
}

.actions {
  margin-left: auto;
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

/* === 加载/空状态 === */

.loading-state {
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  padding: 80px 20px;
  gap: 16px;
  color: #a0aec0;
}

.spinner {
  width: 40px;
  height: 40px;
  border: 4px solid #2a3449;
  border-top-color: #2962ff;
  border-radius: 50%;
  animation: spin 1s linear infinite;
}

@keyframes spin {
  to { transform: rotate(360deg); }
}

.empty-state {
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  padding: 80px 20px;
  color: #a0aec0;
  gap: 12px;
}

.empty-icon {
  font-size: 48px;
  opacity: 0.5;
}

.empty-text {
  font-size: 16px;
  font-weight: 500;
}

.empty-hint {
  font-size: 13px;
  opacity: 0.7;
}

/* === 结果展示区 === */

.results-container {
  flex: 1;
  overflow-y: auto;
  padding: 20px 24px;
  display: flex;
  flex-direction: column;
  gap: 20px;
}

.result-section {
  background: rgba(42, 52, 77, 0.3);
  border-radius: 12px;
  border: 1px solid #2a3449;
  overflow: hidden;
  min-height: 400px;
}

.section-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 14px 20px;
  background: rgba(42, 52, 77, 0.6);
  border-bottom: 1px solid #2a3449;
}

.section-header h3 {
  margin: 0;
  font-size: 15px;
  font-weight: 600;
  color: #e0e0e0;
}

.status-badge {
  padding: 4px 12px;
  border-radius: 12px;
  font-size: 12px;
  font-weight: 600;
}

.status-badge.normal {
  background: rgba(0, 200, 83, 0.2);
  color: #00c853;
  border: 1px solid #00c853;
}

.status-badge.warning {
  background: rgba(255, 23, 68, 0.2);
  color: #ff1744;
  border: 1px solid #ff1744;
}
</style>
