<template>
  <div class="result-panel">
    <div v-if="!result" class="stage-empty">
      <div class="stage-icon">04</div>
      <div>
        <span class="section-eyebrow">RESULTS</span>
        <h3>训练完成后再查看结果分析</h3>
        <p>完成训练后这里展示评估指标、特征重要性、SHAP 解释、诊断图表，并支持模型发布与历史模型切换。</p>
      </div>
    </div>

    <template v-else>
      <!-- 模型选择 + 模型管理 -->
      <div class="model-bar">
        <div class="model-bar-left">
          <span class="section-eyebrow">CURRENT MODEL</span>
          <div class="model-pills">
            <span class="pill primary">#{{ result.model_id }}</span>
            <span class="pill">{{ result.model_type || 'xgboost' }}</span>
            <span class="pill">{{ result.features.length }} features</span>
          </div>
        </div>
        <div class="model-bar-right">
          <button class="btn btn-secondary" @click="refreshModelList" :disabled="loadingList">
            <i v-if="loadingList" class="fas fa-spinner fa-spin"></i>
            {{ loadingList ? '刷新中…' : '历史模型' }}
          </button>
          <button class="btn btn-primary" @click="onBind" :disabled="binding || !canBind" :title="canBind ? '' : bindTooltip">
            <i v-if="binding" class="fas fa-spinner fa-spin"></i>
            {{ binding ? '应用中…' : '应用到策略' }}
          </button>
        </div>
      </div>

      <!-- 历史模型列表（展开） -->
      <div v-if="showModelList" class="model-list-section">
        <div class="section-heading">
          <div>
            <span class="section-eyebrow">MODELS</span>
            <h3 class="section-title">历史模型</h3>
          </div>
          <span class="section-hint">点击行查看对应模型的指标与图表</span>
        </div>

        <div v-if="bindings.length" class="bind-list-section">
          <div class="section-heading">
            <div>
              <span class="section-eyebrow">BOUND</span>
              <h3 class="section-title">已绑定到当前策略</h3>
            </div>
          </div>
          <div class="bind-table">
            <div v-for="b in bindings" :key="b.label" class="bind-row">
              <span class="bind-badge">BOUND</span>
              <span class="bind-label">{{ b.label }}</span>
              <span class="bind-version">v{{ b.version }}</span>
              <span class="bind-path">production/{{ currentStrategyName }}-{{ b.label }}.json</span>
            </div>
          </div>
        </div>

        <div v-if="modelList" class="model-table">
          <div v-if="modelList.production" class="model-row production">
            <span class="model-badge">PROD</span>
            <span class="model-name">{{ modelList.production.filename }}</span>
            <span class="model-meta">{{ modelList.production.meta?.model_type || 'xgboost' }}</span>
            <span class="model-meta">{{ modelList.production.meta?.created_at || '—' }}</span>
          </div>
          <div
            v-for="m in modelList.experiments"
            :key="m.path"
            class="model-row"
            :class="{ active: activeModelPath === m.path }"
            @click="onSelectModel(m)"
          >
            <span class="model-badge">EXP</span>
            <span class="model-name">{{ m.filename }}</span>
            <span class="model-meta">{{ m.meta?.model_type || 'xgboost' }}</span>
            <span class="model-meta">{{ m.meta?.created_at || '—' }}</span>
          </div>
        </div>
      </div>

      <!-- 评估指标 -->
      <div v-if="result.eval_metrics" class="section">
        <div class="section-heading">
          <div>
            <span class="section-eyebrow">EVALUATION</span>
            <h3 class="section-title">评估指标</h3>
          </div>
        </div>
        <MetricsCard :metrics="result.eval_metrics" />
      </div>

      <!-- 诊断图表 -->
      <div v-if="result.predictions?.length" class="section">
        <div class="section-heading">
          <div>
            <span class="section-eyebrow">{{ isClassification ? 'CLASSIFICATION' : 'REGRESSION' }} REPORT</span>
            <h3 class="section-title">{{ isClassification ? '分类诊断' : '回归诊断' }}</h3>
          </div>
        </div>
        <div v-if="isClassification" class="chart-grid">
          <div class="chart-card">
            <RocCurveChart :predictions="result.predictions" :objective="objective" />
          </div>
          <div class="chart-card">
            <ProbabilityDistChart v-if="isBinary" :predictions="result.predictions" />
            <div v-else class="chart-placeholder">
              <i class="fas fa-layer-group"></i>
              <span>多分类概率分布建议查看混淆矩阵</span>
            </div>
          </div>
        </div>
        <div v-else class="chart-grid">
          <div class="chart-card full">
            <PredVsActualChart :predictions="result.predictions" />
          </div>
          <div class="chart-card full">
            <ResidualDistChart :predictions="result.predictions" />
          </div>
        </div>
      </div>

      <div v-if="result.predictions?.length" class="section">
        <div class="section-heading">
          <div>
            <span class="section-eyebrow">CONFUSION</span>
            <h3 class="section-title">混淆矩阵</h3>
          </div>
        </div>
        <ConfusionMatrixChart :predictions="result.predictions" :objective="objective" />
      </div>

      <!-- 特征重要性 -->
      <div class="section">
        <div class="section-heading">
          <div>
            <span class="section-eyebrow">FEATURE IMPORTANCE</span>
            <h3 class="section-title">特征重要性</h3>
          </div>
          <div class="metric-toggle">
            <button v-for="m in METRICS" :key="m.value"
                    :class="{ active: currentMetric === m.value }"
                    @click="setMetric(m.value)">
              {{ m.label }}
            </button>
          </div>
        </div>
        <FeatureImportanceChart :data="result.feature_importance" :metric="currentMetric" />
      </div>

      <!-- SHAP -->
      <div class="section">
        <div class="section-heading">
          <div>
            <span class="section-eyebrow">SHAP</span>
            <h3 class="section-title">SHAP 特征贡献</h3>
          </div>
          <button class="btn btn-primary" :disabled="shapLoading || !result" @click="onComputeShap">
            {{ shapLoading ? '计算中…' : (props.state.trainResult.shap ? '重新计算 SHAP' : '计算 SHAP') }}
          </button>
        </div>
        <div v-if="props.state.trainResult.shap" class="shap-info">
          <span class="info-pill">📊 {{ props.state.trainResult.shap.n_samples }} 个测试样本</span>
          <span class="info-pill">📐 {{ props.state.trainResult.shap.features.length }} 个特征</span>
          <span class="info-pill">✅ base_value + ΣSHAP ≈ prediction</span>
        </div>
        <ShapSummaryChart v-if="props.state.trainResult.shap" :data="props.state.trainResult.shap" />
      </div>
    </template>
  </div>
</template>

<script setup lang="ts">
import { ref, computed } from 'vue'
import { ElMessage } from 'element-plus'
import { useMLData } from '../composables/useMLData'
import { useModelBinding } from '../composables/useModelBinding'
import { useHistoryStore, type Strategy } from '../../../stores/history'
import MetricsCard from '../charts/MetricsCard.vue'
import RocCurveChart from '../charts/RocCurveChart.vue'
import ConfusionMatrixChart from '../charts/ConfusionMatrixChart.vue'
import ProbabilityDistChart from '../charts/ProbabilityDistChart.vue'
import PredVsActualChart from '../charts/PredVsActualChart.vue'
import ResidualDistChart from '../charts/ResidualDistChart.vue'
import FeatureImportanceChart from '../charts/FeatureImportanceChart.vue'
import ShapSummaryChart from '../charts/ShapSummaryChart.vue'

type Metric = 'gain' | 'weight' | 'cover'

const METRICS: { value: Metric; label: string }[] = [
  { value: 'gain', label: 'Gain · 总增益' },
  { value: 'weight', label: 'Weight · 使用次数' },
  { value: 'cover', label: 'Cover · 覆盖样本' },
]

const props = defineProps<{ state: any; selectedStrategyId?: string }>()
const { shap, listModels } = useMLData()
const { binding, bindModel } = useModelBinding()
const strategyStore = useHistoryStore()

const result = computed(() => props.state.trainResult.data)
const objective = computed(() => props.state.config.objective)
const isClassification = computed(() => {
  return objective.value === 'binary:logistic' || objective.value === 'multi:softprob'
})
const isBinary = computed(() => objective.value === 'binary:logistic')

const currentMetric = ref<Metric>('gain')
const shapLoading = ref(false)

// 模型管理
const showModelList = ref(false)
const loadingList = ref(false)
const modelList = ref<{
  experiments: Array<{ path: string; filename: string; meta: any }>
  production: { path: string; filename: string; meta: any } | null
} | null>(null)
const activeModelPath = ref<string>('')

// 当前下拉选择的策略
const currentStrategyName = computed(() => {
  const id = props.selectedStrategyId
  return strategyStore.strategies.find((s: Strategy) => s.id === id)?.name || ''
})
const nodeLabel = computed(() => result.value?.node_label || '')

// 当前策略已绑定的模型列表
const bindings = computed(() => {
  const id = props.selectedStrategyId
  const s = strategyStore.strategies.find((st: Strategy) => st.id === id)
  const models = (s as any)?.data?.models
  return Array.isArray(models) ? models.filter((m: any) => m?.label) : []
})

// bind 按钮可用条件：训练已完成 + 有 model_id + 有当前策略 + 有 nodeLabel
const canBind = computed(() => {
  return !!(result.value?.model_id && currentStrategyName.value && nodeLabel.value)
})
const bindTooltip = computed(() => {
  if (!result.value?.model_id) return '请先完成训练'
  if (!currentStrategyName.value) return '请先在顶部下拉选择目标策略'
  if (!nodeLabel.value) return '当前策略中未找到 XGBoostNode 节点'
  return ''
})

function setMetric(m: Metric) {
  currentMetric.value = m
}

async function onComputeShap() {
  if (!result.value) return
  shapLoading.value = true
  try {
    const sh = await shap(result.value.model_id)
    props.state.trainResult.shap = sh
  } finally {
    shapLoading.value = false
  }
}

async function refreshModelList() {
  showModelList.value = !showModelList.value
  if (!showModelList.value) return
  loadingList.value = true
  try {
    modelList.value = await listModels()
  } finally {
    loadingList.value = false
  }
}

function onSelectModel(m: { path: string; filename: string; meta: any }) {
  activeModelPath.value = m.path
  // 当前模型数据已在 result 中；切换历史模型只展示 meta 信息
  if (m.meta) {
    ElMessage.info(`已选中 ${m.filename}，完整曲线需重新加载该模型（当前显示训练结果）`)
  }
}

async function onBind() {
  if (!result.value?.model_id || !currentStrategyName.value || !nodeLabel.value) return
  await bindModel(
    result.value.model_id,
    currentStrategyName.value,
    nodeLabel.value,
    (bindings) => updateStrategyBindings(bindings),
  )
}

/**
 * 把 binding 信息写入当前策略的 graph.data.models。
 * 静默覆盖：若同 label 已存在则更新 version/bound_at，否则追加。
 */
function updateStrategyBindings(bindings: { label: string; version: string; bound_at: string }[]) {
  const id = props.selectedStrategyId
  const idx = strategyStore.strategies.findIndex(s => s.id === id)
  if (idx < 0) return
  const strategy = strategyStore.strategies[idx]
  const strategyName = strategy.name
  if (!strategy.data) strategy.data = {}
  const existing: any[] = Array.isArray(strategy.data.models) ? strategy.data.models : []
  for (const b of bindings) {
    const i = existing.findIndex(m => m.label === b.label)
    if (i >= 0) existing[i] = b
    else existing.push(b)
    // 同步更新 XGBoostNode 的 modelFile param：production/{strategyName}-{label}.json
    if (strategy.graph?.nodes) {
      for (const n of strategy.graph.nodes) {
        if (n?.data?.nodeType === 'xgboost' && n.data.label === b.label) {
          if (!n.data.params) n.data.params = {}
          n.data.params.modelFile = { value: `production/${strategyName}-${b.label}.json`, type: 'text' }
        }
      }
    }
  }
  strategy.data.models = existing
  strategyStore.strategies[idx] = strategy
  strategyStore.persistStrategies()
}
</script>

<style scoped>
.result-panel { padding: 12px 4px 24px; }

.stage-empty {
  display: flex;
  gap: 18px;
  margin: 8px;
  padding: 28px 32px;
  background: rgba(15, 25, 41, 0.6);
  border: 1px dashed rgba(74, 85, 104, 0.5);
  border-radius: 10px;
  align-items: flex-start;
}
.stage-icon {
  font-size: 28px;
  font-weight: 600;
  color: #5b8ff9;
  background: rgba(91, 143, 249, 0.12);
  border: 1px solid rgba(91, 143, 249, 0.35);
  width: 56px;
  height: 56px;
  border-radius: 12px;
  display: flex;
  align-items: center;
  justify-content: center;
  flex-shrink: 0;
  letter-spacing: 1px;
}
.stage-empty h3 {
  margin: 6px 0 8px;
  font-size: 17px;
  color: #f1f5f9;
  font-weight: 600;
}
.stage-empty p {
  margin: 0;
  color: #94a3b8;
  font-size: 12.5px;
  line-height: 1.6;
  max-width: 540px;
}

.model-bar {
  display: flex;
  justify-content: space-between;
  align-items: center;
  gap: 16px;
  margin-bottom: 18px;
  padding: 14px 20px;
  background: linear-gradient(135deg, rgba(41, 98, 255, 0.18), rgba(41, 98, 255, 0.04) 70%, transparent);
  border: 1px solid rgba(74, 85, 104, 0.35);
  border-radius: 10px;
}
.model-bar-left { display: flex; flex-direction: column; gap: 4px; }
.model-pills { display: flex; gap: 6px; }
.model-bar-right { display: flex; gap: 8px; }

.pill {
  font-size: 11px;
  padding: 2px 10px;
  border-radius: 999px;
  background: rgba(91, 143, 249, 0.12);
  color: #93c5fd;
  font-family: 'SF Mono', 'Consolas', monospace;
}
.pill.primary {
  background: rgba(38, 166, 91, 0.18);
  color: #34d399;
}

.btn {
  padding: 6px 16px;
  border-radius: 4px;
  font-size: 12px;
  cursor: pointer;
  border: none;
  transition: all 0.2s;
}
.btn-primary { background: #3b82f6; color: white; }
.btn-primary:hover:not(:disabled) { background: #2563eb; }
.btn-primary:disabled { background: #475569; cursor: not-allowed; opacity: 0.7; }
.btn-secondary {
  background: rgba(91, 143, 249, 0.12);
  color: #93c5fd;
  border: 1px solid rgba(91, 143, 249, 0.35);
}
.btn-secondary:hover:not(:disabled) { background: rgba(91, 143, 249, 0.2); }
.btn-secondary:disabled { opacity: 0.5; cursor: not-allowed; }

.model-list-section {
  margin-bottom: 18px;
  background: rgba(15, 25, 41, 0.55);
  border: 1px solid rgba(74, 85, 104, 0.25);
  border-radius: 8px;
  padding: 14px 16px 16px;
}
.model-table { display: flex; flex-direction: column; gap: 4px; }
.model-row {
  display: flex;
  align-items: center;
  gap: 12px;
  padding: 8px 12px;
  border-radius: 4px;
  cursor: pointer;
  transition: background 0.15s;
  font-size: 12px;
}
.model-row:hover { background: rgba(74, 85, 104, 0.15); }
.model-row.active { background: rgba(91, 143, 249, 0.18); }
.model-row.production { background: rgba(38, 166, 91, 0.08); }

.bind-list-section { margin-bottom: 14px; }
.bind-table { display: flex; flex-direction: column; gap: 6px; }
.bind-row {
  display: flex; align-items: center; gap: 10px;
  padding: 8px 14px;
  background: rgba(91, 143, 249, 0.06);
  border: 1px solid rgba(91, 143, 249, 0.25);
  border-radius: 8px;
  font-size: 12px;
}
.bind-badge {
  font-size: 10px; padding: 2px 8px;
  background: rgba(91, 143, 249, 0.2); color: #93c5fd;
  border-radius: 999px; font-family: 'SF Mono','Consolas',monospace;
}
.bind-label { color: #f1f5f9; font-weight: 600; min-width: 100px; }
.bind-version {
  color: #34d399; font-family: 'SF Mono','Consolas',monospace;
  font-size: 11px; padding: 2px 6px;
  background: rgba(38, 166, 91, 0.12);
  border-radius: 4px;
}
.bind-path {
  color: #94a3b8; font-family: 'SF Mono','Consolas',monospace;
  font-size: 11px; flex: 1; text-align: right;
  overflow: hidden; text-overflow: ellipsis; white-space: nowrap;
}
.model-badge {
  display: inline-block;
  font-size: 10px;
  padding: 2px 8px;
  border-radius: 3px;
  background: rgba(91, 143, 249, 0.18);
  color: #93c5fd;
  font-weight: 600;
  letter-spacing: 1px;
}
.model-row.production .model-badge {
  background: rgba(38, 166, 91, 0.18);
  color: #34d399;
}
.model-name {
  font-family: 'SF Mono', 'Consolas', monospace;
  color: #e2e8f0;
  flex: 1;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}
.model-meta {
  font-size: 11px;
  color: #94a3b8;
  font-family: 'SF Mono', 'Consolas', monospace;
  min-width: 80px;
}

.section {
  margin-bottom: 18px;
  background: rgba(15, 25, 41, 0.55);
  border: 1px solid rgba(74, 85, 104, 0.25);
  border-radius: 8px;
  padding: 14px 16px 16px;
}
.section-eyebrow {
  font-size: 10.5px;
  letter-spacing: 2.2px;
  color: #5b8ff9;
  font-weight: 600;
  text-transform: uppercase;
}
.section-heading {
  display: flex;
  justify-content: space-between;
  align-items: flex-end;
  margin-bottom: 12px;
}
.section-title {
  color: #e2e8f0;
  font-size: 15px;
  margin: 4px 0 0;
  font-weight: 600;
}
.section-hint { font-size: 11.5px; color: #94a3b8; }

.metric-toggle { display: flex; gap: 4px; }
.metric-toggle button {
  background: transparent;
  border: 1px solid #2b3a55;
  color: #94a3b8;
  padding: 4px 12px;
  font-size: 12px;
  border-radius: 4px;
  cursor: pointer;
}
.metric-toggle button.active {
  background: #3b82f6;
  color: white;
  border-color: #3b82f6;
}

.chart-grid {
  display: grid;
  grid-template-columns: repeat(2, 1fr);
  gap: 16px;
}
.chart-card {
  background: #0f1929;
  border: 1px solid rgba(74, 85, 104, 0.25);
  border-radius: 6px;
  padding: 12px;
  min-height: 320px;
}
.chart-card.full { grid-column: span 2; }
.chart-placeholder {
  height: 100%;
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  color: #64748b;
  gap: 6px;
  font-size: 12px;
}
.chart-placeholder i { font-size: 24px; color: #5b8ff9; }

.shap-info {
  display: flex;
  gap: 12px;
  margin-bottom: 12px;
}
.info-pill {
  background: #2b3a55;
  color: #94a3b8;
  padding: 4px 10px;
  border-radius: 12px;
  font-size: 12px;
}
</style>
