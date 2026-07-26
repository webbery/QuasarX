<template>
  <div class="xgboost-tab">
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
      :loading="state.labelAnalysis.loading"
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
      </template>
    </AnalysisControlBar>

    <!-- 标签分析图表 + 控制面板 -->
    <div v-if="state.labelAnalysis.result" class="label-chart-section">
      <LabelAnalysisChart :data="state.labelAnalysis.result" />

      <!-- 标签分布条 -->
      <div class="label-distribution">
        <div class="dist-bar">
          <div class="dist-seg up" :style="{ width: labelStats.upPct + '%' }"></div>
          <div class="dist-seg flat" :style="{ width: labelStats.flatPct + '%' }"></div>
          <div class="dist-seg down" :style="{ width: labelStats.downPct + '%' }"></div>
        </div>
        <div class="dist-stats">
          <span class="dist-item"><i class="dot up"></i>UP {{ labelStats.up }} ({{ labelStats.upPct.toFixed(1) }}%)</span>
          <span class="dist-item"><i class="dot flat"></i>FLAT {{ labelStats.flat }} ({{ labelStats.flatPct.toFixed(1) }}%)</span>
          <span class="dist-item"><i class="dot down"></i>DOWN {{ labelStats.down }} ({{ labelStats.downPct.toFixed(1) }}%)</span>
          <span class="dist-threshold">阈值: {{ (state.labelAnalysis.result.threshold * 100).toFixed(2) }}%</span>
        </div>
      </div>

      <!-- 参数控制 -->
      <div class="label-controls">
        <div class="control-row">
          <label>预测周期 N:</label>
          <input type="range" min="1" max="30" step="1" v-model.number="state.config.labelPeriod" />
          <input type="number" min="1" max="60" v-model.number="state.config.labelPeriod" class="num-input" />
          <span class="unit">天</span>
        </div>
        <div class="control-row">
          <label>阈值系数 vol_k:</label>
          <input type="range" min="0.1" max="2.0" step="0.05" v-model.number="state.config.volK" />
          <input type="number" min="0.1" max="2.0" step="0.1" v-model.number="state.config.volK" class="num-input" />
        </div>
        <div class="control-row batch-row">
          <button class="btn-batch" :disabled="state.batchAnalysis.loading || availableSecurities.length === 0" @click="runBatchAnalysis">
            {{ state.batchAnalysis.loading ? state.batchAnalysis.progress : '批量标签分析' }}
          </button>
          <span class="batch-hint">对策略全部 {{ availableSecurities.length }} 只标的计算标签分布（N={{ state.config.labelPeriod }}, vol_k={{ state.config.volK.toFixed(2) }}）</span>
        </div>
      </div>

      <!-- 批量分析结果表 -->
      <div v-if="state.batchAnalysis.results.length > 0" class="batch-table-section">
        <table class="batch-table">
          <thead>
            <tr>
              <th>标的</th>
              <th>UP</th>
              <th>FLAT</th>
              <th>DOWN</th>
              <th>总数</th>
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
        <!-- 翻页 -->
        <div v-if="batchTotalPages > 1" class="pagination">
          <button class="page-btn" :disabled="batchPage <= 1" @click="batchPage--">‹</button>
          <span class="page-info">{{ batchPage }} / {{ batchTotalPages }}</span>
          <button class="page-btn" :disabled="batchPage >= batchTotalPages" @click="batchPage++">›</button>
        </div>
      </div>
    </div>

    <!-- 内容 -->
    <div v-if="!selectedStrategyId" class="empty-tab">
      <div class="empty-icon">🧠</div>
      <h2>XGBoost 训练与分析</h2>
      <p>选择一个使用 XGBoost 节点的策略后开始训练</p>
      <p class="note">支持的训练流程：<br />
        • 部分回测：仅执行 XGBoost 上游节点收集特征数据<br />
        • 离线训练：调用 xgboost (Python) 训练模型<br />
        • SHAP 分析：C++ 端 XGBoost C API 计算特征贡献
      </p>
    </div>

    <div v-else-if="!hasXGBoost" class="empty-tab">
      <div class="empty-icon">⚠️</div>
      <h2>策略中未找到 XGBoost 节点</h2>
      <p>请在策略图中添加 XGBoost 节点后再使用此面板</p>
    </div>

    <template v-else>
      <el-tabs v-model="activeTab" class="sub-tabs">
        <el-tab-pane label="训练" name="train">
          <TrainPanel :state="state" :script="script" @trained="onTrained" />
        </el-tab-pane>
        <el-tab-pane label="特征分析" name="feature">
          <FeaturePanel :state="state" />
        </el-tab-pane>
        <el-tab-pane label="诊断" name="diagnostic">
          <DiagnosticPanel :state="state" />
        </el-tab-pane>
      </el-tabs>
    </template>
  </div>
</template>

<script setup lang="ts">
import { ref, computed, onMounted, watch } from 'vue'
import { useXGBoostState } from './composables/useXGBoostState'
import { useXGBoostData } from './composables/useXGBoostData'
import TrainPanel from './panels/TrainPanel.vue'
import FeaturePanel from './panels/FeaturePanel.vue'
import DiagnosticPanel from './panels/DiagnosticPanel.vue'
import LabelAnalysisChart from './charts/LabelAnalysisChart.vue'
import AnalysisControlBar from '@/components/shared/AnalysisControlBar.vue'
import { useStrategySecurities } from '@/components/shared/composables/useStrategySecurities'
import { useHistoryStore } from '@/stores/history'

const activeTab = ref('train')
const state = useXGBoostState()
const { field, quickRange, frequency, dateRange, labelSymbol, QUICK_RANGES, setQuickRange, setFrequency, setField } = state
const { fetchLabelAnalysis, runBatchLabelAnalysis } = useXGBoostData()

const {
  strategyOptions,
  selectedStrategyId,
  availableSecurities,
  checkedSymbols,
  loading: securitiesLoading,
  loadSecuritiesForStrategy,
  toggleSymbol,
} = useStrategySecurities({ defaultCheckAll: false })
const historyStore = useHistoryStore()

const script = ref('')
const hasXGBoost = ref(false)

// 标签标的选项（从已勾选标的中取）
const labelSymbolOptions = computed(() => Array.from(checkedSymbols.value))

// 是否可以运行标签分析
const canRunLabel = computed(() => {
  return !!selectedStrategyId.value
    && checkedSymbols.value.size > 0
    && !!labelSymbol.value
    && !!dateRange.value
})

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
    hasXGBoost.value = false
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
    // 补充策略 ID（parse_strategy_script_v2 需要 content["id"]）
    graphData.id = selectedStrategyId.value
    script.value = JSON.stringify(graphData)
    hasXGBoost.value = graphData.nodes?.some((n: any) => n?.data?.nodeType === 'xgboost') ?? false
  } catch (e) {
    console.error('[XGBoostTab] 加载策略失败:', e)
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

/** 判断某行是否极端不平衡 */
function isImbalanced(stat: any): boolean {
  return stat.upPct < 10 || stat.downPct < 10 || stat.upPct > 55 || stat.downPct > 55 || stat.flatPct > 70
}

// 批量结果翻页
const batchPage = ref(1)
const BATCH_PAGE_SIZE = 15
const batchTotalPages = computed(() => Math.ceil(state.batchAnalysis.results.length / BATCH_PAGE_SIZE) || 1)
const paginatedBatchResults = computed(() => {
  const start = (batchPage.value - 1) * BATCH_PAGE_SIZE
  return state.batchAnalysis.results.slice(start, start + BATCH_PAGE_SIZE)
})

// 批量分析完成后重置到第一页
watch(() => state.batchAnalysis.results.length, () => { batchPage.value = 1 })

function onTrained() {
  activeTab.value = 'feature'
}

// 监听策略 ID 变化
watch(selectedStrategyId, (newId) => {
  if (newId) {
    onStrategyChange()
  } else {
    script.value = ''
    hasXGBoost.value = false
    state.reset()
  }
})

// 当已勾选标的变化时，自动选择标签标的
watch(checkedSymbols, (syms) => {
  if (syms.size === 0) {
    labelSymbol.value = ''
  } else if (!syms.has(labelSymbol.value)) {
    labelSymbol.value = Array.from(syms)[0]
  }
}, { deep: false })

// 预测周期 / 阈值系数变化时，实时重算标签（无需网络请求）
watch(() => state.config.labelPeriod, () => state.recomputeLabels())
watch(() => state.config.volK, () => state.recomputeLabels())

// 标签分布统计
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
.xgboost-tab {
  display: flex;
  flex-direction: column;
  height: 100%;
  background: #1a2236;
  color: #e0e0e0;
}

.field-selector,
.label-symbol-selector {
  display: flex;
  align-items: center;
  gap: 6px;
}

.field-selector label,
.label-symbol-selector label {
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

.label-chart-section {
  flex: 1;
  min-height: 0;
  overflow-y: auto;
  padding: 8px;
  background: rgba(26, 34, 54, 0.5);
  border-bottom: 1px solid rgba(74, 85, 104, 0.2);
  scrollbar-width: thin;
  scrollbar-color: rgba(255, 255, 255, 0.1) transparent;
}
.label-chart-section::-webkit-scrollbar {
  width: 6px;
}
.label-chart-section::-webkit-scrollbar-track {
  background: transparent;
}
.label-chart-section::-webkit-scrollbar-thumb {
  background: rgba(255, 255, 255, 0.1);
  border-radius: 3px;
}

/* 标签分布条 */
.label-distribution {
  padding: 8px 12px;
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
.dist-stats {
  display: flex;
  align-items: center;
  gap: 16px;
  font-size: 11px;
  color: #999;
}
.dist-item {
  display: flex;
  align-items: center;
  gap: 4px;
}
.dot {
  width: 8px;
  height: 8px;
  border-radius: 50%;
  display: inline-block;
}
.dot.up { background: #26a65b; }
.dot.flat { background: #9e9e9e; }
.dot.down { background: #ea3943; }
.dist-threshold {
  margin-left: auto;
  color: #5b8ff9;
  font-family: 'SF Mono', 'Consolas', monospace;
}

/* 参数控制 */
.label-controls {
  padding: 8px 12px 12px;
  display: flex;
  flex-direction: column;
  gap: 8px;
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

/* 批量分析按钮 */
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

/* 批量结果表 */
.batch-table-section {
  padding: 0 12px 12px;
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

/* 翻页 */
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

.empty-tab {
  padding: 80px 20px;
  text-align: center;
  color: #64748b;
}
.empty-tab h2 { color: #cbd5e1; margin: 16px 0 8px; }
.empty-tab .note {
  margin-top: 24px;
  font-size: 13px;
  color: #94a3b8;
  line-height: 1.8;
}
.empty-icon { font-size: 64px; margin-bottom: 16px; }
.sub-tabs {
  flex: 1;
  background: #131c2e;
  border-radius: 8px;
  padding: 8px 16px;
  margin: 8px;
  overflow: auto;
}
:deep(.el-tabs__item) {
  color: #94a3b8;
  font-size: 14px;
}
:deep(.el-tabs__item.is-active) {
  color: #3b82f6;
}
</style>
