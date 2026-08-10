<template>
  <div class="ml-tab">
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
      :loading="loadingAny"
      :can-analyze="canRunLabel"
      run-label="标签分析"
      show-frequency
      @update:quickRange="setQuickRange($event)"
      @update:frequency="setFrequency($event)"
      @update-date-range="updateDateRange"
      @toggle-symbol="toggleSymbol"
      @run-analysis="runLabelAnalysis"
    >
      <template #extra-controls>
        <div class="field-selector">
          <label>字段:</label>
          <select :value="field" class="select-small" @change="setField(($event.target as HTMLSelectElement).value)">
            <option value="close">C 收盘价</option>
            <option value="open">O 开盘价</option>
            <option value="high">H 最高价</option>
            <option value="low">L 最低价</option>
            <option value="volume">V 成交量</option>
          </select>
        </div>
        <div class="label-symbol-selector">
          <label>标签标的:</label>
          <select :value="labelSymbol" class="select-small" @change="labelSymbol = ($event.target as HTMLSelectElement).value">
            <option value="">选择标的</option>
            <option v-for="opt in labelSymbolOptions" :key="opt" :value="opt">{{ opt }}</option>
          </select>
        </div>
        <div class="label-shape-selector">
          <label>标签形状:</label>
          <select v-model="config.labelShape" class="select-small">
            <option v-for="s in LABEL_SHAPES" :key="s.value" :value="s.value">{{ s.label }}</option>
          </select>
        </div>
      </template>
    </AnalysisControlBar>

    <div v-if="!selectedStrategyId" class="stage-empty stage-empty-hero">
      <div class="stage-icon">00</div>
      <div>
        <h3>选择一个含 ML 节点的策略</h3>
        <p>工作流：特征分析 → 标签分析 → 训练分析 → 结果分析，各阶段独立可自由切换。</p>
      </div>
    </div>

    <div v-else-if="!hasMLNode" class="stage-empty">
      <div class="stage-icon">⚠️</div>
      <div>
        <h3>策略中未找到 ML 节点</h3>
        <p>请在策略图中加入 XGBoost（或未来支持的 ML 节点）后，再回到此面板使用训练与分析功能。</p>
      </div>
    </div>

    <template v-else>
      <div class="compact-status">
        <span class="node-status"><i></i>ML</span>
        <span class="status-sep">·</span>
        <span>{{ selectedStrategyName }}</span>
        <span class="status-sep">·</span>
        <span>{{ dateRange?.[0] || '—' }} → {{ dateRange?.[1] || '—' }}</span>
        <span class="status-sep">·</span>
        <span>{{ frequency }}</span>
        <span v-if="state.trainResult.data" class="status-sep">·</span>
        <span v-if="state.trainResult.data">模型 #{{ state.trainResult.data.model_id }}</span>
      </div>

      <div class="card-nav">
        <button
          v-for="step in STEPS"
          :key="step.key"
          :class="['card-btn', { active: activeTab === step.key }]"
          @click="activeTab = step.key"
        >
          <span class="card-icon">{{ step.icon }}</span>
          <span class="card-label">{{ step.label }}</span>
        </button>
      </div>

      <div class="card-body">
        <div v-show="activeTab === 'feature'">
          <FeatureAnalysisPanel :state="state" :script="script" />
        </div>

        <div v-show="activeTab === 'label'" class="label-workspace">
          <div v-if="state.labelAnalysis.result" class="label-result">
            <LabelAnalysisChart :data="state.labelAnalysis.result" />

            <div class="label-distribution">
              <div class="dist-heading">
                <span class="dist-label">标签分布</span>
                <span class="dist-threshold">阈值 {{ (state.labelAnalysis.result.threshold * 100).toFixed(2) }}%</span>
              </div>
              <div class="dist-bar">
                <div class="dist-seg up" :style="{ width: labelStats.upPct + '%' }"></div>
                <div class="dist-seg flat" :style="{ width: labelStats.flatPct + '%' }"></div>
                <div class="dist-seg down" :style="{ width: labelStats.downPct + '%' }"></div>
              </div>
              <div class="dist-stats">
                <span class="dist-item"><i class="dot up"></i><b>UP</b>{{ labelStats.up }} · {{ labelStats.upPct.toFixed(1) }}%</span>
                <span class="dist-item"><i class="dot flat"></i><b>FLAT</b>{{ labelStats.flat }} · {{ labelStats.flatPct.toFixed(1) }}%</span>
                <span class="dist-item"><i class="dot down"></i><b>DOWN</b>{{ labelStats.down }} · {{ labelStats.downPct.toFixed(1) }}%</span>
                <span class="dist-count">有效样本 {{ labelStats.total }}</span>
              </div>
            </div>

            <div class="label-controls">
              <div class="controls-header">
                <span class="controls-title">参数调节</span>
                <span class="controls-hint">拖动后本地重算</span>
              </div>
              <div class="control-row">
                <label>预测周期 N</label>
                <input type="range" min="1" max="30" step="1" v-model.number="state.config.labelPeriod" />
                <input type="number" min="1" max="60" v-model.number="state.config.labelPeriod" class="num-input" />
                <span class="unit">天</span>
              </div>
              <div class="control-row">
                <label>波动阈值 vol_k</label>
                <input type="range" min="0.1" max="2.0" step="0.05" v-model.number="state.config.volK" />
                <input type="number" min="0.1" max="2.0" step="0.1" v-model.number="state.config.volK" class="num-input" />
              </div>
              <div class="batch-row">
                <button class="btn-batch" :disabled="state.batchAnalysis.loading || availableSecurities.length === 0" @click="runBatchAnalysis">
                  {{ state.batchAnalysis.loading ? state.batchAnalysis.progress : '扫描全部标的' }}
                </button>
                <span class="batch-hint">对策略内 {{ availableSecurities.length }} 只标的检查标签分布</span>
              </div>
            </div>

            <div v-if="state.batchAnalysis.results.length > 0" class="batch-table-section">
              <div class="controls-header" style="margin-bottom: 8px">
                <span class="controls-title">标的间标签分布</span>
                <span class="controls-hint">红色行表示类别失衡</span>
              </div>
              <table class="batch-table">
                <thead>
                  <tr>
                    <th>标的</th>
                    <th>UP</th>
                    <th>FLAT</th>
                    <th>DOWN</th>
                    <th>样本</th>
                    <th>分布</th>
                  </tr>
                </thead>
                <tbody>
                  <tr v-for="stat in paginatedBatchResults" :key="stat.symbol" :class="{ imbalanced: isImbalanced(stat) }">
                    <td class="sym-cell">{{ stat.symbol }}</td>
                    <td class="pct-cell up-text">{{ stat.upPct.toFixed(1) }}%</td>
                    <td class="pct-cell flat-text">{{ stat.flatPct.toFixed(1) }}%</td>
                    <td class="pct-cell down-text">{{ stat.downPct.toFixed(1) }}%</td>
                    <td class="num-cell">{{ stat.total }}</td>
                    <td class="bar-cell">
                      <div class="mini-bar">
                        <div class="mini-seg up" :style="{ width: stat.upPct + '%' }"></div>
                        <div class="mini-seg flat" :style="{ width: stat.flatPct + '%' }"></div>
                        <div class="mini-seg down" :style="{ width: stat.downPct + '%' }"></div>
                      </div>
                    </td>
                  </tr>
                </tbody>
              </table>
              <div v-if="batchTotalPages > 1" class="pagination">
                <button class="page-btn" :disabled="batchPage <= 1" @click="batchPage--">‹</button>
                <span class="page-info">{{ batchPage }} / {{ batchTotalPages }}</span>
                <button class="page-btn" :disabled="batchPage >= batchTotalPages" @click="batchPage++">›</button>
              </div>
            </div>
          </div>
          <div v-else class="stage-empty">
            <div class="stage-icon">02</div>
            <div>
              <h3>确认标签是否适合训练</h3>
              <p>选择标签标的、字段和日期范围，运行标签分析后再决定预测周期与波动阈值。</p>
              <button class="empty-action" :disabled="!canRunLabel" @click="runLabelAnalysis">运行标签分析</button>
            </div>
          </div>
        </div>

        <div v-show="activeTab === 'train'">
          <TrainPanel :state="state" :script="script" @trained="onTrained" />
        </div>

        <div v-show="activeTab === 'result'">
          <ResultPanel :state="state" :selected-strategy-id="selectedStrategyId" />
        </div>
      </div>
    </template>
  </div>
</template>

<script setup lang="ts">
import { ref, computed, onMounted, watch } from 'vue'
import { useMLState, LABEL_SHAPES } from './composables/useMLState'
import { useMLData } from './composables/useMLData'
import FeatureAnalysisPanel from './panels/FeatureAnalysisPanel.vue'
import TrainPanel from './panels/TrainPanel.vue'
import ResultPanel from './panels/ResultPanel.vue'
import LabelAnalysisChart from './charts/LabelAnalysisChart.vue'
import AnalysisControlBar from '@/components/shared/AnalysisControlBar.vue'
import { useStrategySecurities } from '@/components/shared/composables/useStrategySecurities'
import { useHistoryStore } from '@/stores/history'

const STEPS = [
  { key: 'feature', label: '特征分析', icon: '🔍' },
  { key: 'label', label: '标签分析', icon: '🏷' },
  { key: 'train', label: '训练分析', icon: '⚡' },
  { key: 'result', label: '结果分析', icon: '📊' },
] as const

const activeTab = ref('feature')
const state = useMLState()
const { field, quickRange, frequency, dateRange, labelSymbol, QUICK_RANGES, setQuickRange, setFrequency, setField } = state
const config = state.config
const { fetchLabelAnalysis, runBatchLabelAnalysis } = useMLData()

const {
  strategyOptions,
  selectedStrategyId,
  availableSecurities,
  checkedSymbols,
  loading: securitiesLoading,
  loadSecuritiesForStrategy,
  toggleSymbol,
} = useStrategySecurities({ defaultCheckAll: true })
const historyStore = useHistoryStore()

const script = ref('')
const hasMLNode = ref(false)

// 标签标的选项（从已勾选标的中取）
const labelSymbolOptions = computed(() => Array.from(checkedSymbols.value))

// 是否可以运行标签分析
const canRunLabel = computed(() => {
  if (!selectedStrategyId.value || checkedSymbols.value.size === 0 || !dateRange.value) return false
  if (config.labelShape === 'vector') return !!labelSymbol.value
  return true  // matrix 模式不需要选择标签标的
})

const loadingAny = computed(() =>
  state.labelAnalysis.loading || state.batchAnalysis.loading || state.featureReport.loading || state.trainResult.loading,
)

function updateDateRange(value: string, type: 'start' | 'end') {
  if (dateRange.value) {
    dateRange.value = type === 'start'
      ? [value, dateRange.value[1]]
      : [dateRange.value[0], value]
  }
}

async function onStrategyChange() {
  if (!selectedStrategyId.value) {
    script.value = ''
    hasMLNode.value = false
    return
  }

  loadSecuritiesForStrategy(selectedStrategyId.value)

  try {
    const versions = await historyStore.getVersionsByStrategy(selectedStrategyId.value)
    if (versions.length === 0) return
    const sorted = [...versions].sort((a, b) =>
      new Date(b.saveTime).getTime() - new Date(a.saveTime).getTime()
    )
    const latest = sorted[0]
    if (!latest) return
    const data = await historyStore.loadVersionFlowData(latest.id)
    if (!data) return
    const graphData = typeof data === 'string' ? JSON.parse(data) : data
    graphData.id = selectedStrategyId.value
    // 切换策略前清空旧 labelSource，避免发往后端的 symbol 指向已删除标的
    // TrainPanel.vue 的 watch 在 script 变化时会用新策略的首位 code 自动重新填充
    state.config.labelSource = ''
    script.value = JSON.stringify(graphData)
    // 检测是否包含任何 ML 节点（当前仅 XGBoost）
    hasMLNode.value = graphData.nodes?.some((n: any) => {
      const t = n?.data?.nodeType
      return t === 'xgboost' || t === 'ml' || t === 'narx'
    }) ?? false
  } catch (e) {
    console.error('[MLTab] 加载策略失败:', e)
  }
}

async function runLabelAnalysis() {
  if (!labelSymbol.value || !dateRange.value) return

  state.labelAnalysis.loading = true
  try {
    const [startDate, endDate] = dateRange.value
    const result = await fetchLabelAnalysis(
      labelSymbol.value,
      field.value,
      startDate,
      endDate,
      frequency.value,
      state.config.labelPeriod,
      state.config.volK,
      state.config.labelType,
      state.priceCache,
    )
    state.labelAnalysis.result = result
  } finally {
    state.labelAnalysis.loading = false
  }
}

async function runBatchAnalysis() {
  const symbols = availableSecurities.value.map(s => s.code)
  if (symbols.length === 0 || !dateRange.value) return

  state.batchAnalysis.loading = true
  state.batchAnalysis.results = []
  try {
    const [startDate, endDate] = dateRange.value
    const results = await runBatchLabelAnalysis(
      symbols,
      field.value,
      startDate,
      endDate,
      frequency.value,
      state.config.labelPeriod,
      state.config.volK,
      state.priceCache,
      (current, total, symbol) => {
        state.batchAnalysis.progress = `${current}/${total} ${symbol}`
      },
    )
    state.batchAnalysis.results = results
  } finally {
    state.batchAnalysis.loading = false
    state.batchAnalysis.progress = ''
  }
}

function isImbalanced(stat: any): boolean {
  return stat.upPct < 10 || stat.downPct < 10 || stat.upPct > 55 || stat.downPct > 55 || stat.flatPct > 70
}

const batchPage = ref(1)
const BATCH_PAGE_SIZE = 15
const batchTotalPages = computed(() => Math.ceil(state.batchAnalysis.results.length / BATCH_PAGE_SIZE) || 1)
const paginatedBatchResults = computed(() => {
  const start = (batchPage.value - 1) * BATCH_PAGE_SIZE
  return state.batchAnalysis.results.slice(start, start + BATCH_PAGE_SIZE)
})

watch(() => state.batchAnalysis.results.length, () => { batchPage.value = 1 })

function onTrained() {
  activeTab.value = 'result'
}

const selectedStrategyName = computed(() => {
  return strategyOptions.value.find(opt => opt.id === selectedStrategyId.value)?.name || '—'
})

watch(selectedStrategyId, (newId) => {
  if (newId) {
    onStrategyChange()
  } else {
    script.value = ''
    hasMLNode.value = false
    state.reset()
  }
})

watch(checkedSymbols, (syms) => {
  if (syms.size === 0) {
    labelSymbol.value = ''
  } else if (!syms.has(labelSymbol.value)) {
    labelSymbol.value = Array.from(syms)[0]
  }
}, { deep: false })

watch(() => state.config.labelPeriod, () => state.recomputeLabels())
watch(() => state.config.volK, () => state.recomputeLabels())

const labelStats = computed(() => {
  const result = state.labelAnalysis.result
  if (!result) return { up: 0, flat: 0, down: 0, total: 0, upPct: 0, flatPct: 0, downPct: 0 }
  const valid = result.labels.filter(l => l >= 0)
  const up = valid.filter(l => l === 0).length
  const flat = valid.filter(l => l === 1).length
  const down = valid.filter(l => l === 2).length
  const total = valid.length
  return {
    up, flat, down, total,
    upPct: total > 0 ? (up / total * 100) : 0,
    flatPct: total > 0 ? (flat / total * 100) : 0,
    downPct: total > 0 ? (down / total * 100) : 0,
  }
})

onMounted(() => {
  if (selectedStrategyId.value) onStrategyChange()
})
</script>

<style scoped>
.ml-tab {
  display: flex;
  flex-direction: column;
  height: 100%;
  background: #1a2236;
  color: #e0e0e0;
}

.field-selector,
.label-symbol-selector,
.label-shape-selector {
  display: flex;
  align-items: center;
  gap: 6px;
}

.field-selector label,
.label-symbol-selector label,
.label-shape-selector label {
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

.dist-bar {
  display: flex;
  height: 8px;
  border-radius: 4px;
  overflow: hidden;
  margin-bottom: 6px;
}
.dist-seg {
  transition: width 0.3s ease;
}
.dist-seg.up { background: #26a65b; }
.dist-seg.flat { background: #9e9e9e; }
.dist-seg.down { background: #ea3943; }
.dot {
  width: 8px;
  height: 8px;
  border-radius: 50%;
  display: inline-block;
}
.dot.up { background: #26a65b; }
.dot.flat { background: #9e9e9e; }
.dot.down { background: #ea3943; }

.label-controls {
  padding: 10px 14px;
  display: flex;
  flex-direction: column;
  gap: 6px;
  background: rgba(15, 25, 41, 0.55);
  border: 1px solid rgba(74, 85, 104, 0.25);
  border-radius: 6px;
}
.control-row {
  display: flex;
  align-items: center;
  gap: 10px;
}
.control-row label {
  font-size: 12px;
  color: #999;
  white-space: nowrap;
  min-width: 100px;
}
.control-row input[type="range"] {
  flex: 1;
  height: 4px;
  -webkit-appearance: none;
  appearance: none;
  background: rgba(74, 85, 104, 0.4);
  border-radius: 2px;
  outline: none;
  cursor: pointer;
}
.control-row input[type="range"]::-webkit-slider-thumb {
  -webkit-appearance: none;
  appearance: none;
  width: 14px;
  height: 14px;
  border-radius: 50%;
  background: #5b8ff9;
  cursor: pointer;
  border: 2px solid #1a2236;
}
.control-row input[type="range"]::-moz-range-thumb {
  width: 14px;
  height: 14px;
  border-radius: 50%;
  background: #5b8ff9;
  cursor: pointer;
  border: 2px solid #1a2236;
}
.num-input {
  width: 60px;
  padding: 3px 6px;
  background: rgba(26, 34, 54, 0.8);
  border: 1px solid rgba(74, 85, 104, 0.3);
  border-radius: 4px;
  color: #e0e0e0;
  font-size: 12px;
  text-align: center;
  outline: none;
  font-family: 'SF Mono', 'Consolas', monospace;
}
.num-input:focus {
  border-color: rgba(41, 98, 255, 0.5);
}
.unit {
  font-size: 11px;
  color: #666;
}

.batch-row {
  margin-top: 4px;
  padding-top: 8px;
  border-top: 1px solid rgba(74, 85, 104, 0.15);
}
.btn-batch {
  padding: 5px 16px;
  background: rgba(41, 98, 255, 0.15);
  border: 1px solid rgba(41, 98, 255, 0.4);
  border-radius: 4px;
  color: #5b8ff9;
  font-size: 12px;
  cursor: pointer;
  transition: all 0.2s;
  white-space: nowrap;
}
.btn-batch:hover:not(:disabled) {
  background: rgba(41, 98, 255, 0.25);
  border-color: rgba(41, 98, 255, 0.6);
}
.btn-batch:disabled {
  opacity: 0.5;
  cursor: not-allowed;
}
.batch-hint {
  font-size: 11px;
  color: #666;
  font-family: 'SF Mono', 'Consolas', monospace;
}

.batch-table-section {
  padding: 0 0 8px;
  overflow-x: auto;
}
.batch-table {
  width: 100%;
  border-collapse: collapse;
  font-size: 12px;
}
.batch-table th {
  text-align: left;
  padding: 6px 8px;
  color: #999;
  font-weight: 500;
  border-bottom: 1px solid rgba(74, 85, 104, 0.3);
  white-space: nowrap;
}
.batch-table td {
  padding: 5px 8px;
  border-bottom: 1px solid rgba(74, 85, 104, 0.1);
}
.batch-table tr.imbalanced {
  background: rgba(234, 57, 67, 0.08);
}
.batch-table tr.imbalanced td {
  border-bottom-color: rgba(234, 57, 67, 0.15);
}
.sym-cell {
  color: #e0e0e0;
  font-family: 'SF Mono', 'Consolas', monospace;
  font-size: 11px;
}
.pct-cell {
  font-family: 'SF Mono', 'Consolas', monospace;
  font-size: 11px;
}
.up-text { color: #26a65b; }
.flat-text { color: #9e9e9e; }
.down-text { color: #ea3943; }
.num-cell {
  color: #999;
  font-family: 'SF Mono', 'Consolas', monospace;
  font-size: 11px;
  text-align: right;
}
.bar-cell {
  min-width: 120px;
}
.mini-bar {
  display: flex;
  height: 6px;
  border-radius: 3px;
  overflow: hidden;
}
.mini-seg {
  height: 100%;
  transition: width 0.3s;
}
.mini-seg.up { background: #26a65b; }
.mini-seg.flat { background: #9e9e9e; }
.mini-seg.down { background: #ea3943; }

.pagination {
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 12px;
  padding: 10px 0 4px;
}
.page-btn {
  width: 28px;
  height: 28px;
  display: flex;
  align-items: center;
  justify-content: center;
  background: rgba(26, 34, 54, 0.8);
  border: 1px solid rgba(74, 85, 104, 0.3);
  border-radius: 4px;
  color: #e0e0e0;
  font-size: 16px;
  cursor: pointer;
  transition: all 0.2s;
}
.page-btn:hover:not(:disabled) {
  border-color: rgba(41, 98, 255, 0.5);
  background: rgba(41, 98, 255, 0.1);
}
.page-btn:disabled {
  opacity: 0.3;
  cursor: not-allowed;
}
.page-info {
  font-size: 12px;
  color: #999;
  font-family: 'SF Mono', 'Consolas', monospace;
  min-width: 60px;
  text-align: center;
}

.card-nav {
  display: flex;
  gap: 8px;
  padding: 8px 12px 0;
}
.card-btn {
  flex: 1;
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 7px;
  padding: 10px 14px;
  background: rgba(15, 25, 41, 0.5);
  border: 1px solid rgba(74, 85, 104, 0.3);
  border-bottom: none;
  border-radius: 8px 8px 0 0;
  color: #64748b;
  font-size: 13px;
  cursor: pointer;
  transition: all 0.2s;
  position: relative;
}
.card-btn:hover {
  background: rgba(20, 32, 55, 0.7);
  color: #94a3b8;
}
.card-btn.active {
  background: #131c2e;
  color: #e2e8f0;
  border-color: rgba(59, 130, 246, 0.4);
  border-bottom: 1px solid #131c2e;
  z-index: 1;
}
.card-btn.active::after {
  content: '';
  position: absolute;
  bottom: 0;
  left: 0;
  right: 0;
  height: 2px;
  background: #3b82f6;
  border-radius: 2px 2px 0 0;
}
.card-icon {
  font-size: 15px;
  line-height: 1;
}
.card-label {
  font-weight: 500;
  white-space: nowrap;
}
.card-body {
  flex: 1;
  background: #131c2e;
  border: 1px solid rgba(59, 130, 246, 0.15);
  border-radius: 0 0 8px 8px;
  padding: 12px;
  margin: 0 12px 8px;
  overflow: auto;
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

.label-workspace {
  display: flex;
  flex-direction: column;
  gap: 10px;
  padding: 4px 4px 16px;
}
.label-result {
  display: flex;
  flex-direction: column;
  gap: 10px;
}
.controls-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  gap: 12px;
}
.controls-title {
  font-size: 13px;
  color: #e2e8f0;
  font-weight: 600;
}
.controls-hint {
  font-size: 11px;
  color: #94a3b8;
}

.label-distribution {
  background: rgba(15, 25, 41, 0.55);
  border: 1px solid rgba(74, 85, 104, 0.25);
  border-radius: 6px;
  padding: 10px 14px;
}
.dist-heading {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 8px;
}
.dist-label {
  font-size: 12px;
  color: #cbd5e1;
  font-weight: 500;
}
.dist-threshold {
  font-family: 'SF Mono', 'Consolas', monospace;
  color: #5b8ff9;
  font-size: 11.5px;
  background: rgba(91, 143, 249, 0.12);
  padding: 2px 8px;
  border-radius: 999px;
}
.dist-stats {
  display: flex;
  flex-wrap: wrap;
  gap: 14px 22px;
  font-size: 11.5px;
  color: #94a3b8;
  margin-top: 10px;
}
.dist-item {
  display: inline-flex;
  align-items: center;
  gap: 6px;
}
.dist-item b {
  font-weight: 600;
  color: #cbd5e1;
  margin-right: 2px;
}
.dist-count {
  margin-left: auto;
  color: #64748b;
  font-family: 'SF Mono', 'Consolas', monospace;
  font-size: 11px;
}

.label-controls {
  background: rgba(15, 25, 41, 0.55);
  border: 1px solid rgba(74, 85, 104, 0.25);
  border-radius: 8px;
  padding: 14px 16px 16px;
}
.batch-table-section {
  background: rgba(15, 25, 41, 0.55);
  border: 1px solid rgba(74, 85, 104, 0.25);
  border-radius: 8px;
  padding: 14px 16px 6px;
}

.stage-empty {
  display: flex;
  gap: 14px;
  margin: 8px;
  padding: 20px 24px;
  background: rgba(15, 25, 41, 0.6);
  border: 1px dashed rgba(74, 85, 104, 0.5);
  border-radius: 8px;
  align-items: flex-start;
}
.stage-empty-hero {
  margin: 12px 8px;
  padding: 24px 28px;
}
.stage-icon {
  font-size: 32px;
  font-weight: 600;
  color: #5b8ff9;
  background: rgba(91, 143, 249, 0.12);
  border: 1px solid rgba(91, 143, 249, 0.35);
  width: 64px;
  height: 64px;
  border-radius: 12px;
  display: flex;
  align-items: center;
  justify-content: center;
  flex-shrink: 0;
  letter-spacing: 1px;
}
.stage-empty h3 {
  margin: 6px 0 8px;
  font-size: 18px;
  color: #f1f5f9;
  font-weight: 600;
}
.stage-empty p {
  margin: 0 0 10px;
  color: #94a3b8;
  font-size: 12.5px;
  line-height: 1.6;
  max-width: 640px;
}
.empty-action {
  background: #3b82f6;
  border: none;
  color: white;
  padding: 6px 16px;
  border-radius: 4px;
  font-size: 12px;
  cursor: pointer;
}
.empty-action:disabled {
  background: #475569;
  cursor: not-allowed;
  opacity: 0.7;
}

/* ── 统一滚动条风格 ── */
.card-body::-webkit-scrollbar,
.batch-table-section::-webkit-scrollbar {
  width: 6px;
  height: 6px;
}
.card-body::-webkit-scrollbar-track,
.batch-table-section::-webkit-scrollbar-track {
  background: transparent;
}
.card-body::-webkit-scrollbar-thumb,
.batch-table-section::-webkit-scrollbar-thumb {
  background: rgba(255, 255, 255, 0.1);
  border-radius: 3px;
}
.card-body::-webkit-scrollbar-thumb:hover,
.batch-table-section::-webkit-scrollbar-thumb:hover {
  background: rgba(255, 255, 255, 0.2);
}
</style>
