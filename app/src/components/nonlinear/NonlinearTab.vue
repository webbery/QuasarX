<template>
  <div class="nonlinear-tab">
    <!-- 顶部控制栏 -->
    <AnalysisControlBar
      v-model:mode="mode"
      v-model:selectedStrategyId="selectedStrategyId"
      v-model:selectedMacroCountry="selectedMacroCountry"
      v-model:selectedMacroIndicator="selectedMacroIndicator"
      v-model:quickRange="state.quickRange"
      :strategy-options="strategyOptions"
      :available-securities="availableSecurities"
      :checked-symbols="checkedSymbols"
      :filtered-macro-options="filteredMacroOptions"
      :quick-ranges="QUICK_RANGES"
      :loading="loading"
      :can-analyze="canAnalyze"
      @update:quickRange="setQuickRange($event)"
      @update-date-range="updateDateRange"
      @toggle-symbol="toggleSymbol"
      @run-analysis="runAnalysis"
    >
      <template #extra-controls>
        <div class="field-selector">
          <label>字段:</label>
          <select v-model="state.field" class="select-small">
            <option value="close">C 收盘价</option>
            <option value="open">O 开盘价</option>
            <option value="high">H 最高价</option>
            <option value="low">L 最低价</option>
            <option value="volume">V 成交量</option>
          </select>
        </div>

        <div class="param-group">
          <label>嵌入维度:</label>
          <input v-model.number="state.embedDim" type="number" min="2" max="7" class="number-input-small" />
        </div>

        <div class="param-group">
          <label>时间延迟:</label>
          <input v-model.number="state.timeDelay" type="number" min="0" max="100" class="number-input-small" title="0 = 自动(互信息法)" />
        </div>

        <div class="param-group">
          <label>Lyapunov:</label>
          <input v-model.number="state.lyapunovHorizon" type="number" min="10" max="200" class="number-input-small" />
        </div>
      </template>
    </AnalysisControlBar>

    <!-- 分析结果 -->
    <div v-if="result" class="results">
      <!-- 诊断摘要 -->
      <section class="diagnosis-section">
        <div class="diagnosis-cards">
          <div class="diag-card">
            <span class="diag-label">Hurst 指数</span>
            <span class="diag-value" :class="{ persistent: result.mmar.hurst > 0.5, anti: result.mmar.hurst < 0.5 }">
              {{ result.mmar.hurst.toFixed(4) }}
            </span>
            <span class="diag-hint">{{ result.mmar.hurst > 0.5 ? '持久性' : result.mmar.hurst < 0.5 ? '反持久性' : '随机' }}</span>
          </div>
          <div class="diag-card">
            <span class="diag-label">多分形谱宽 Δα</span>
            <span class="diag-value">{{ result.mmar.width.toFixed(4) }}</span>
            <span class="diag-hint">{{ result.mmar.width > 0.1 ? '强多分形' : '弱多分形' }}</span>
          </div>
          <div class="diag-card">
            <span class="diag-label">关联维数 D₂</span>
            <span class="diag-value">{{ result.phase_space.correlation_dimension.toFixed(3) }}</span>
            <span class="diag-hint">m = {{ result.phase_space.embed_dim }}</span>
          </div>
          <div class="diag-card">
            <span class="diag-label">最大 Lyapunov λ</span>
            <span class="diag-value" :class="{ positive: result.phase_space.max_lyapunov > 0.001 }">
              {{ result.phase_space.max_lyapunov.toFixed(4) }}
            </span>
            <span class="diag-hint">{{ result.phase_space.max_lyapunov > 0.001 ? '敏感依赖' : '非混沌' }}</span>
          </div>
          <div class="diag-card full-width">
            <span class="diag-label">诊断</span>
            <span class="diag-diagnosis" :class="{ deterministic: result.phase_space.is_deterministic }">
              {{ result.phase_space.diagnosis }}
            </span>
          </div>
        </div>
      </section>

      <!-- MMAR 多分形分析 -->
      <section class="section">
        <h3 class="section-title">MMAR 多分形分析</h3>
        <div class="chart-grid">
          <div class="chart-card">
            <MMARChart :data="result.mmar" />
          </div>
          <div class="chart-card">
            <HqChart :data="result.mmar" />
          </div>
        </div>
      </section>

      <!-- 相空间重构 -->
      <section class="section">
        <h3 class="section-title">相空间重构与吸引子检测</h3>
        <div class="chart-grid">
          <div class="chart-card">
            <PhaseSpaceChart :data="result.phase_space" />
          </div>
          <div class="chart-card">
            <CorrelationDimChart :data="result.phase_space" />
          </div>
        </div>
        <div class="chart-grid" style="margin-top: 12px;">
          <div class="chart-card">
            <LyapunovChart :data="result.phase_space" />
          </div>
          <div class="chart-card info-card">
            <h4>参数信息</h4>
            <div class="info-grid">
              <div class="info-item">
                <span class="info-label">数据点数</span>
                <span class="info-value">{{ result.data_points }}</span>
              </div>
              <div class="info-item">
                <span class="info-label">嵌入维度 m</span>
                <span class="info-value">{{ result.phase_space.embed_dim }}</span>
              </div>
              <div class="info-item">
                <span class="info-label">时间延迟 τ</span>
                <span class="info-value">{{ result.phase_space.time_delay }} ({{ result.phase_space.delay_method === 'mi' ? '互信息法' : '固定值' }})</span>
              </div>
              <div class="info-item">
                <span class="info-label">轨迹点数</span>
                <span class="info-value">{{ result.phase_space.trajectory.length }}</span>
              </div>
            </div>
          </div>
        </div>
      </section>
    </div>

    <!-- 空状态 -->
    <div v-else-if="!loading" class="empty-state">
      <p>选择标的和参数后点击"开始分析"</p>
      <p class="hint">支持 MMAR 多分形分析和相空间重构吸引子检测</p>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, computed, watch } from 'vue'
import { useNonlinearState, QUICK_RANGES } from './composables/useNonlinearState'
import { useNonlinearData } from './composables/useNonlinearData'
import { useStrategySecurities } from '../shared/composables/useStrategySecurities'
import { useMacroIndicators } from '../shared/composables/useMacroIndicators'
import AnalysisControlBar from '../shared/AnalysisControlBar.vue'
import MMARChart from './charts/MMARChart.vue'
import HqChart from './charts/HqChart.vue'
import PhaseSpaceChart from './charts/PhaseSpaceChart.vue'
import CorrelationDimChart from './charts/CorrelationDimChart.vue'
import LyapunovChart from './charts/LyapunovChart.vue'

const { state, result, setQuickRange } = useNonlinearState()
const { loading: fetchLoading, fetchNonlinear } = useNonlinearData()
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

type AnalysisMode = 'strategy' | 'macro'
const mode = ref<AnalysisMode>('strategy')
const selectedMacroCountry = ref('china')
const selectedMacroIndicator = ref('')

const filteredMacroOptions = computed(() =>
  macroOptionsByCountry.value[selectedMacroCountry.value] || []
)

const loading = computed(() => securitiesLoading.value || fetchLoading.value)

const canAnalyze = computed(() => {
  if (loading.value) return false
  if (mode.value === 'macro') return !!selectedMacroIndicator.value
  return checkedSymbols.value.size > 0
})

watch(checkedSymbols, (next) => {
  if (mode.value !== 'strategy') return
  // 非线性分析只取第一个标的
  const arr = Array.from(next)
  if (arr.length > 0) {
    checkedSymbols.value = new Set([arr[0]])
  }
})

watch(selectedStrategyId, (newId) => {
  if (newId) {
    loadSecuritiesForStrategy(newId)
  } else {
    availableSecurities.value = []
    checkedSymbols.value = new Set()
  }
})

watch([selectedMacroCountry, selectedMacroIndicator], ([country, indicator]) => {
  if (mode.value !== 'macro' || !indicator) return
  // macro mode
})

function updateDateRange(value: string, type: 'start' | 'end') {
  if (state.dateRange) {
    state.dateRange = type === 'start'
      ? [value, state.dateRange[1]]
      : [state.dateRange[0], value]
  }
}

async function runAnalysis() {
  let symbol = ''
  if (mode.value === 'macro') {
    symbol = `${selectedMacroCountry.value}/${selectedMacroIndicator.value}`
  } else {
    const first = Array.from(checkedSymbols.value)[0]
    if (!first) return
    symbol = first
  }

  if (!state.dateRange) return

  const res = await fetchNonlinear(
    symbol,
    state.dateRange[0],
    state.dateRange[1],
    state.field,
    {
      qMin: state.qMin,
      qMax: state.qMax,
      qStep: state.qStep,
      minWindow: state.minWindow,
      embedDim: state.embedDim,
      timeDelay: state.timeDelay,
      lyapunovHorizon: state.lyapunovHorizon,
    }
  )
  if (res) result.value = res
}
</script>

<style scoped>
.nonlinear-tab {
  display: flex;
  flex-direction: column;
  height: 100%;
  overflow-y: auto;
  padding: 12px;
}

.results {
  display: flex;
  flex-direction: column;
  gap: 16px;
  margin-top: 12px;
}

.section {
  background: rgba(0, 0, 0, 0.2);
  border-radius: 8px;
  border: 1px solid var(--border, #333);
  padding: 16px;
}

.section-title {
  margin: 0 0 12px 0;
  font-size: 1rem;
  color: var(--text, #e0e0e0);
  font-weight: 600;
}

.chart-grid {
  display: grid;
  grid-template-columns: repeat(2, 1fr);
  gap: 12px;
}

.chart-card {
  background: rgba(0, 0, 0, 0.15);
  border-radius: 6px;
  border: 1px solid rgba(255, 255, 255, 0.06);
  padding: 8px;
  min-height: 320px;
}

/* 诊断摘要卡片 */
.diagnosis-section {
  margin-bottom: 4px;
}

.diagnosis-cards {
  display: flex;
  gap: 12px;
  flex-wrap: wrap;
}

.diag-card {
  flex: 1;
  min-width: 140px;
  background: rgba(0, 0, 0, 0.3);
  border-radius: 8px;
  border: 1px solid var(--border, #333);
  padding: 12px 16px;
  display: flex;
  flex-direction: column;
  gap: 4px;
}

.diag-card.full-width {
  flex: 1 1 100%;
}

.diag-label {
  font-size: 0.75rem;
  color: var(--text-secondary, #999);
  text-transform: uppercase;
  letter-spacing: 0.5px;
}

.diag-value {
  font-size: 1.3rem;
  font-weight: 700;
  color: var(--text, #e0e0e0);
  font-family: 'Courier New', monospace;
}

.diag-value.persistent { color: #00c853; }
.diag-value.anti { color: #ff6d00; }
.diag-value.positive { color: #ff1744; }

.diag-hint {
  font-size: 0.75rem;
  color: var(--text-secondary, #666);
}

.diag-diagnosis {
  font-size: 0.9rem;
  color: var(--text-secondary, #999);
  padding: 4px 0;
}

.diag-diagnosis.deterministic {
  color: #00c853;
  font-weight: 600;
}

/* 信息卡片 */
.info-card {
  display: flex;
  flex-direction: column;
  gap: 12px;
}

.info-card h4 {
  margin: 0;
  font-size: 0.95rem;
  color: var(--text, #e0e0e0);
}

.info-grid {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 10px;
}

.info-item {
  display: flex;
  flex-direction: column;
  gap: 2px;
}

.info-label {
  font-size: 0.75rem;
  color: var(--text-secondary, #999);
}

.info-value {
  font-size: 0.9rem;
  color: var(--text, #e0e0e0);
  font-family: 'Courier New', monospace;
}

/* 控制栏扩展 */
.field-selector,
.param-group {
  display: flex;
  align-items: center;
  gap: 4px;
}

.field-selector label,
.param-group label {
  font-size: 0.8rem;
  color: var(--text-secondary, #999);
  white-space: nowrap;
}

.select-small,
.number-input-small {
  background: rgba(0, 0, 0, 0.3);
  border: 1px solid var(--border, #444);
  border-radius: 4px;
  color: var(--text, #e0e0e0);
  padding: 4px 6px;
  font-size: 0.8rem;
  width: 60px;
}

.number-input-small {
  width: 55px;
}

/* 空状态 */
.empty-state {
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  height: 300px;
  color: var(--text-secondary, #666);
}

.empty-state p {
  margin: 4px 0;
}

.empty-state .hint {
  font-size: 0.85rem;
  color: var(--text-secondary, #555);
}

@media (max-width: 900px) {
  .chart-grid {
    grid-template-columns: 1fr;
  }
}
</style>
