<template>
  <div class="pca-tab">
    <!-- 顶部控制栏 -->
    <div class="control-bar">
      <!-- 模式切换 -->
      <div class="mode-toggle">
        <button
          :class="['mode-btn', { active: state.mode === 'cross_section' }]"
          @click="state.mode = 'cross_section'"
        >截面分析 (多标的)</button>
        <button
          :class="['mode-btn', { active: state.mode === 'time_series' }]"
          @click="state.mode = 'time_series'"
        >时序分析 (单标的多特征)</button>
      </div>

      <!-- 截面模式：标的选择 -->
      <template v-if="state.mode === 'cross_section'">
        <div class="symbol-input">
          <label>标的:</label>
          <input
            v-model="state.editingSymbol"
            @keyup.enter="addSymbolInput"
            placeholder="输入标的代码，回车添加"
            class="input-small"
          />
          <button class="add-btn" @click="addSymbolInput">添加</button>
        </div>

        <!-- 已选标的 tag -->
        <div v-if="state.symbols.length > 0" class="selected-symbols">
          <span class="selected-label">已选 ({{ state.symbols.length }}):</span>
          <span
            v-for="(sym, idx) in state.symbols"
            :key="sym"
            class="symbol-tag"
          >
            {{ sym }}
            <button class="tag-close" @click="removeSymbol(idx)">×</button>
          </span>
        </div>
      </template>

      <!-- 时序模式：单标的选择 -->
      <template v-else>
        <div class="symbol-input">
          <label>标的:</label>
          <input
            v-model="state.editingSymbol"
            @keyup.enter="addSymbolInput"
            placeholder="输入标的代码"
            class="input-small"
          />
          <button class="add-btn" @click="addSymbolInput">确定</button>
        </div>
        <div v-if="state.symbols.length > 0" class="selected-symbols">
          <span class="selected-label">当前标的:</span>
          <span class="symbol-tag">
            {{ state.symbols[0] }}
            <button class="tag-close" @click="removeSymbol(0)">×</button>
          </span>
        </div>
      </template>

      <!-- 字段选择 -->
      <div class="control-group">
        <label>数据字段:</label>
        <select v-model="state.field" class="select-small">
          <option value="return">收益率 (推荐)</option>
          <option value="close">收盘价</option>
          <option value="volume">成交量</option>
        </select>
      </div>

      <!-- PC 数量 -->
      <div class="control-group">
        <label>PC 数量:</label>
        <select v-model.number="state.nComponents" class="select-small">
          <option :value="0">自动 (Kaiser 准则)</option>
          <option :value="2">2</option>
          <option :value="3">3</option>
          <option :value="4">4</option>
          <option :value="5">5</option>
        </select>
      </div>

      <!-- 填充方式 -->
      <div class="control-group">
        <label>缺失值填充:</label>
        <select v-model="state.fillMethod" class="select-small">
          <option value="Forward">向前填充</option>
          <option value="None">不填充</option>
        </select>
      </div>

      <!-- 时间范围 -->
      <div class="control-group">
        <label>时间范围:</label>
        <select v-model="state.quickRange" @change="setQuickRange(state.quickRange)" class="select-small">
          <option v-for="[label] in QUICK_RANGES" :key="label" :value="label">{{ label }}</option>
        </select>
      </div>

      <!-- 分析按钮 -->
      <button
        class="analyze-btn"
        :disabled="loading || state.symbols.length === 0"
        @click="runPCA"
      >
        {{ loading ? '分析中...' : '开始 PCA 分析' }}
      </button>
    </div>

    <!-- 错误提示 -->
    <div v-if="error" class="error-msg">{{ error }}</div>

    <!-- Loading -->
    <div v-if="loading" class="loading-msg">正在执行 PCA 分析...</div>

    <!-- 结果展示 -->
    <template v-if="result && !loading">
      <!-- 质量评估指标卡片 -->
      <PCAMetricsCard
        :quality="qualityMetrics"
      />

      <!-- 碎石图 + 方差解释 -->
      <div class="charts-row">
        <ScreePlot
          v-if="crossSectionResult"
          :data="crossSectionResult"
          :selected-k="selectedKValue"
          @update:k="selectedK = $event"
        />
        <VarianceExplained
          v-if="crossSectionResult || timeSeriesResult"
          :eigenvalues="currentEigenvalues"
          :variance_ratio="currentVarianceRatio"
          :cumulative_variance="currentCumulativeVariance"
          :selected-k="selectedKValue"
        />
      </div>

      <!-- 载荷热力图 -->
      <LoadingHeatmap
        v-if="crossSectionResult || timeSeriesResult"
        :symbols="currentSymbolsOrFeatures"
        :loadings="currentLoadings"
        :n-components="selectedKValue"
      />

      <!-- 得分时序 -->
      <ScoreTimeSeries
        v-if="(crossSectionResult || timeSeriesResult) && result.dates"
        :dates="result.dates"
        :scores="currentScores"
        :n-components="selectedKValue"
      />

      <!-- 相关矩阵对比 (仅截面模式) -->
      <CorrelationComparison
        v-if="crossSectionResult"
        :symbols="crossSectionResult.symbols"
        :original="crossSectionResult.corr_original"
        :reconstructed="crossSectionResult.corr_reconstructed"
        :n-components="selectedKValue"
      />
    </template>
  </div>
</template>

<script setup lang="ts">
import { computed } from 'vue'
import { usePCAState, usePCAData } from './index'
import PCAMetricsCard from './charts/PCAMetricsCard.vue'
import ScreePlot from './charts/ScreePlot.vue'
import LoadingHeatmap from './charts/LoadingHeatmap.vue'
import ScoreTimeSeries from './charts/ScoreTimeSeries.vue'
import CorrelationComparison from './charts/CorrelationComparison.vue'
import VarianceExplained from './charts/VarianceExplained.vue'

const { state, result, selectedK, QUICK_RANGES, addSymbol, removeSymbol, setQuickRange } = usePCAState()
const { loading, error, fetchPCA } = usePCAData()

function addSymbolInput() {
  const sym = state.editingSymbol.trim()
  if (!sym) return

  if (state.mode === 'time_series') {
    // 时序模式：只保留一个标的
    state.symbols = [sym]
  } else {
    addSymbol(sym)
  }
  state.editingSymbol = ''
}

async function runPCA() {
  const res = await fetchPCA(
    state.symbols,
    state.dateRange,
    state.field,
    state.nComponents,
    state.mode,
    state.fillMethod
  )
  if (res) {
    result.value = res
    // 自动设置 selectedK
    const cs = res.cross_section
    const ts = res.time_series
    const nComp = cs?.n_components ?? ts?.n_components ?? 3
    selectedK.value = nComp
  }
}

// 便捷访问
const crossSectionResult = computed(() => result.value?.mode === 'cross_section' ? result.value?.cross_section : null)
const timeSeriesResult = computed(() => result.value?.mode === 'time_series' ? result.value?.time_series : null)
const qualityMetrics = computed(() => {
  if (crossSectionResult.value) return crossSectionResult.value.quality
  if (timeSeriesResult.value) return timeSeriesResult.value.quality
  return undefined
})

const selectedKValue = computed(() => {
  const maxK = crossSectionResult.value?.n_symbols ?? timeSeriesResult.value?.n_features ?? 5
  return selectedK.value > 0 ? Math.min(selectedK.value, maxK) : maxK
})

// 当前模式下的数据
const currentEigenvalues = computed(() => {
  return crossSectionResult.value?.eigenvalues ?? timeSeriesResult.value?.eigenvalues ?? []
})
const currentVarianceRatio = computed(() => {
  return crossSectionResult.value?.variance_ratio ?? timeSeriesResult.value?.variance_ratio ?? []
})
const currentCumulativeVariance = computed(() => {
  return crossSectionResult.value?.cumulative_variance ?? timeSeriesResult.value?.cumulative_variance ?? []
})
const currentLoadings = computed(() => {
  return crossSectionResult.value?.loadings ?? timeSeriesResult.value?.loadings ?? []
})
const currentScores = computed(() => {
  return crossSectionResult.value?.scores ?? timeSeriesResult.value?.scores ?? []
})
const currentSymbolsOrFeatures = computed(() => {
  if (crossSectionResult.value) return crossSectionResult.value.symbols
  if (timeSeriesResult.value) return timeSeriesResult.value.features
  return []
})
</script>

<style scoped>
.pca-tab {
  display: flex;
  flex-direction: column;
  gap: 16px;
  padding: 8px;
}

/* 控制栏 */
.control-bar {
  display: flex;
  flex-wrap: wrap;
  gap: 12px;
  align-items: center;
  background: rgba(26, 34, 54, 0.9);
  border-radius: 8px;
  padding: 12px;
}

.mode-toggle {
  display: flex;
  gap: 4px;
}

.mode-btn {
  background: rgba(30, 40, 60, 0.8);
  border: 1px solid rgba(74, 85, 104, 0.5);
  color: #999;
  padding: 6px 12px;
  border-radius: 4px;
  cursor: pointer;
  font-size: 12px;
  transition: all 0.2s;
}

.mode-btn.active {
  background: #2962ff;
  color: #fff;
  border-color: #2962ff;
}

.symbol-input {
  display: flex;
  align-items: center;
  gap: 6px;
}

.input-small {
  background: rgba(30, 40, 60, 0.8);
  border: 1px solid rgba(74, 85, 104, 0.5);
  color: #e0e0e0;
  padding: 4px 8px;
  border-radius: 4px;
  font-size: 12px;
  width: 120px;
}

.add-btn {
  background: #2962ff;
  color: #fff;
  border: none;
  padding: 4px 10px;
  border-radius: 4px;
  cursor: pointer;
  font-size: 12px;
}

.selected-symbols {
  display: flex;
  align-items: center;
  gap: 6px;
  flex-wrap: wrap;
}

.selected-label {
  color: #999;
  font-size: 12px;
}

.symbol-tag {
  background: rgba(41, 98, 255, 0.2);
  border: 1px solid #2962ff;
  color: #e0e0e0;
  padding: 2px 8px;
  border-radius: 12px;
  font-size: 11px;
  display: flex;
  align-items: center;
  gap: 4px;
}

.tag-close {
  background: none;
  border: none;
  color: #ef232a;
  cursor: pointer;
  font-size: 14px;
  padding: 0 2px;
  line-height: 1;
}

.control-group {
  display: flex;
  align-items: center;
  gap: 6px;
}

.control-group label {
  color: #999;
  font-size: 12px;
}

.select-small {
  background: rgba(30, 40, 60, 0.8);
  border: 1px solid rgba(74, 85, 104, 0.5);
  color: #e0e0e0;
  padding: 4px 8px;
  border-radius: 4px;
  font-size: 12px;
}

.analyze-btn {
  background: #00c853;
  color: #fff;
  border: none;
  padding: 6px 16px;
  border-radius: 4px;
  cursor: pointer;
  font-size: 13px;
  font-weight: bold;
  transition: all 0.2s;
}

.analyze-btn:disabled {
  background: #555;
  cursor: not-allowed;
}

.analyze-btn:hover:not(:disabled) {
  background: #00e676;
}

.error-msg {
  background: rgba(239, 35, 42, 0.1);
  border: 1px solid #ef232a;
  color: #ef232a;
  padding: 8px 12px;
  border-radius: 6px;
  font-size: 13px;
}

.loading-msg {
  color: #ff9800;
  padding: 16px;
  text-align: center;
  font-size: 14px;
}

/* 图表行 */
.charts-row {
  display: flex;
  gap: 16px;
}

.charts-row > * {
  flex: 1;
  min-width: 0;
}
</style>
