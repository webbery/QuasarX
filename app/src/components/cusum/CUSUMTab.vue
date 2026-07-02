<template>
  <div class="cusum-tab">
    <!-- 头部控制区 -->
    <header class="cusum-header">
      <div class="control-group">
        <label>标的池：</label>
        <div class="symbol-input">
          <input
            v-model="symbolInput"
            type="text"
            class="symbol-text"
            placeholder="输入标的，逗号分隔，如 sh.600519,sz.000001"
          />
          <button class="btn btn-small" @click="parseSymbols">解析</button>
        </div>
        <div class="symbol-tags" v-if="symbols.length > 0">
          <span v-for="sym in symbols" :key="sym" class="tag">{{ sym }}</span>
        </div>
      </div>

      <div class="control-group">
        <label>时间范围：</label>
        <input type="date" v-model="startDate" class="date-input" />
        <span class="date-separator">至</span>
        <input type="date" v-model="endDate" class="date-input" />
      </div>

      <div class="control-group">
        <label>CUSUM 参数：</label>
        <span class="param-label">λ</span>
        <input type="number" v-model.number="lambda" class="param-input" step="0.1" min="0.1" max="2.0" />
        <span class="param-label">h</span>
        <input type="number" v-model.number="threshold" class="param-input" step="0.5" min="1.0" max="10.0" />
        <span class="param-label">min_obs</span>
        <input type="number" v-model.number="minObs" class="param-input" step="5" min="10" max="100" />
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
        <button class="btn btn-primary" @click="runAnalysis" :disabled="loading || symbols.length === 0">
          {{ loading ? '分析中...' : '▶ 开始分析' }}
        </button>
        <button class="btn btn-secondary" @click="resetForm">↻ 重置</button>
      </div>
    </header>

    <!-- 加载状态 -->
    <div v-if="loading" class="loading-state">
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
    <div v-else class="results-container">
      <!-- 1. 均值漂移检测 -->
      <div v-if="modes.mean && result.mean_cusum" class="result-section">
        <div class="section-header">
          <h3>📊 均值漂移检测 (Mean Shift)</h3>
          <span :class="['status-badge', result.mean_cusum.change_points.length > 0 ? 'warning' : 'normal']">
            {{ result.mean_cusum.change_points.length > 0 ? `⚠ ${result.mean_cusum.change_points.length} 次变点` : '✓ 正常' }}
          </span>
        </div>
        <MeanShiftChart
          :s-pos="result.mean_cusum.s_pos"
          :s-neg="result.mean_cusum.s_neg"
          :threshold="result.mean_cusum.threshold"
          :change-points="result.mean_cusum.change_points"
          :dates="result.dates"
        />
      </div>

      <!-- 2. 方差漂移检测 -->
      <div v-if="modes.variance && result.variance_cusum" class="result-section">
        <div class="section-header">
          <h3>📈 方差漂移检测 (Variance Shift)</h3>
          <span :class="['status-badge', result.variance_cusum.change_points.length > 0 ? 'warning' : 'normal']">
            {{ result.variance_cusum.change_points.length > 0 ? `⚠ ${result.variance_cusum.change_points.length} 次变点` : '✓ 正常' }}
          </span>
        </div>
        <VarianceShiftChart
          :s-pos="result.variance_cusum.s_pos"
          :s-neg="result.variance_cusum.s_neg"
          :threshold="result.variance_cusum.threshold"
          :change-points="result.variance_cusum.change_points"
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
        />
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, reactive } from 'vue'
import MeanShiftChart from './charts/MeanShiftChart.vue'
import VarianceShiftChart from './charts/VarianceShiftChart.vue'
import CorrelationChart from './charts/CorrelationChart.vue'
import TimelineChart from './charts/TimelineChart.vue'

// === 表单状态 ===
const symbolInput = ref('')
const symbols = ref<string[]>([])
const startDate = ref('2024-01-01')
const endDate = ref('2024-12-31')
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

// === 方法 ===

function parseSymbols() {
  symbols.value = symbolInput.value
    .split(/[,，\s]+/)
    .map(s => s.trim().toLowerCase())
    .filter(s => s.length > 0)
}

async function runAnalysis() {
  if (symbols.value.length === 0) {
    alert('请先输入标的池')
    return
  }

  loading.value = true
  hasResult.value = false

  try {
    const activeModes = []
    if (modes.mean) activeModes.push('mean')
    if (modes.variance) activeModes.push('variance')
    if (modes.correlation) activeModes.push('correlation')

    const response = await fetch('/v0/analysis/cusum', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        symbols: symbols.value,
        start: startDate.value,
        end: endDate.value,
        lambda: lambda.value,
        threshold_multiplier: threshold.value,
        min_obs: minObs.value,
        modes: activeModes,
      }),
    })

    if (!response.ok) {
      throw new Error(`HTTP ${response.status}: ${response.statusText}`)
    }

    result.value = await response.json()
    hasResult.value = true
  } catch (err: any) {
    console.error('[CUSUMTab] Analysis error:', err)
    alert(`分析失败：${err.message}`)
  } finally {
    loading.value = false
  }
}

function resetForm() {
  symbolInput.value = ''
  symbols.value = []
  startDate.value = '2024-01-01'
  endDate.value = '2024-12-31'
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
  gap: 16px;
  padding: 16px 24px;
  background: rgba(42, 52, 77, 0.6);
  border-bottom: 1px solid #2a3449;
  align-items: flex-end;
}

.control-group {
  display: flex;
  align-items: center;
  gap: 8px;
}

.control-group label {
  font-size: 13px;
  font-weight: 600;
  color: #a0aec0;
  white-space: nowrap;
}

.symbol-input {
  display: flex;
  gap: 8px;
}

.symbol-text {
  width: 280px;
  padding: 6px 10px;
  background: #2a3449;
  border: 1px solid #3a4459;
  border-radius: 6px;
  color: #e0e0e0;
  font-size: 13px;
}

.symbol-text:focus {
  outline: none;
  border-color: #2962ff;
}

.symbol-tags {
  display: flex;
  gap: 6px;
  flex-wrap: wrap;
}

.tag {
  padding: 2px 8px;
  background: rgba(41, 98, 255, 0.2);
  border: 1px solid #2962ff;
  border-radius: 4px;
  font-size: 12px;
  color: #64b5f6;
}

.date-input {
  padding: 6px 10px;
  background: #2a3449;
  border: 1px solid #3a4459;
  border-radius: 6px;
  color: #e0e0e0;
  font-size: 13px;
}

.date-separator {
  color: #a0aec0;
  font-size: 13px;
}

.param-label {
  font-size: 13px;
  color: #a0aec0;
  font-weight: 600;
}

.param-input {
  width: 60px;
  padding: 6px 8px;
  background: #2a3449;
  border: 1px solid #3a4459;
  border-radius: 6px;
  color: #e0e0e0;
  font-size: 13px;
  text-align: center;
}

.checkbox-label {
  display: flex;
  align-items: center;
  gap: 4px;
  font-size: 13px;
  color: #e0e0e0;
  cursor: pointer;
}

.checkbox-label input[type="checkbox"] {
  accent-color: #2962ff;
}

.actions {
  margin-left: auto;
}

.btn {
  padding: 8px 16px;
  border: none;
  border-radius: 6px;
  font-size: 13px;
  font-weight: 600;
  cursor: pointer;
  transition: all 0.2s;
}

.btn-primary {
  background: #2962ff;
  color: #fff;
}

.btn-primary:hover:not(:disabled) {
  background: #1e4fd9;
}

.btn-primary:disabled {
  opacity: 0.5;
  cursor: not-allowed;
}

.btn-secondary {
  background: #3a4459;
  color: #e0e0e0;
  margin-left: 8px;
}

.btn-secondary:hover {
  background: #4a5469;
}

.btn-small {
  padding: 4px 10px;
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
