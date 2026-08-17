<template>
  <div class="model-manage-panel">
    <!-- 空状态 -->
    <div v-if="!props.selectedStrategyId" class="stage-empty">
      <div class="stage-icon">📦</div>
      <div>
        <h3>请先选择策略</h3>
        <p>模型管理展示当前策略训练的所有模型，支持对比、绑定和删除。</p>
      </div>
    </div>

    <template v-else>
      <!-- 工具栏 -->
      <div class="toolbar">
        <div class="toolbar-left">
          <button class="btn btn-secondary" @click="refresh" :disabled="loading">
            {{ loading ? '加载中…' : '刷新' }}
          </button>
          <button
            v-if="experiments.length >= 2"
            :class="['btn', compareMode ? 'btn-primary' : 'btn-secondary']"
            @click="compareMode = !compareMode; if (!compareMode) selectedPaths.clear()"
          >
            {{ compareMode ? '退出对比' : '对比模式' }}
          </button>
          <span v-if="compareMode && selectedPaths.size > 0" class="compare-hint">
            已选 {{ selectedPaths.size }} 个模型
          </span>
        </div>
        <div class="toolbar-right">
          <span class="model-count">共 {{ experiments.length }} 个实验模型</span>
        </div>
      </div>

      <!-- 生产模型 -->
      <div v-if="production" class="section production-section">
        <div class="section-heading">
          <div>
            <span class="section-eyebrow">PRODUCTION</span>
            <h3 class="section-title">当前生产模型</h3>
          </div>
        </div>
        <div class="prod-row">
          <span class="badge prod">PROD</span>
          <span class="prod-name">{{ production.filename }}</span>
          <span class="meta">{{ production.meta?.model_type || 'xgboost' }}</span>
          <span class="meta">{{ production.meta?.created_at || '—' }}</span>
          <span v-if="production.meta?.n_train" class="meta">
            {{ production.meta.n_train }}/{{ production.meta.n_test }} samples
          </span>
        </div>
      </div>

      <!-- 实验模型列表 -->
      <div class="section">
        <div class="section-heading">
          <div>
            <span class="section-eyebrow">EXPERIMENTS</span>
            <h3 class="section-title">实验模型</h3>
          </div>
          <span class="section-hint">点击行展开详情</span>
        </div>

        <div v-if="loading && !experiments.length" class="loading-state">加载中…</div>
        <div v-else-if="!experiments.length" class="empty-state">
          暂无实验模型，完成训练后模型将自动保存。
        </div>

        <div v-else class="model-table">
          <div class="table-header">
            <div v-if="compareMode" class="col-check"></div>
            <div class="col-id">ID</div>
            <div class="col-name">文件名</div>
            <div class="col-type">类型</div>
            <div class="col-time">创建时间</div>
            <div class="col-samples">样本</div>
            <div class="col-acc">Accuracy</div>
            <div class="col-actions">操作</div>
          </div>

          <template v-for="m in experiments" :key="m.path">
            <div
              class="table-row"
              :class="{ active: expandedPath === m.path, bound: isBound(m) }"
              @click="expandedPath = expandedPath === m.path ? '' : m.path"
            >
              <div v-if="compareMode" class="col-check" @click.stop>
                <input type="checkbox" :checked="selectedPaths.has(m.path)" @change="toggleSelect(m.path)" />
              </div>
              <div class="col-id">
                <span class="id-pill">#{{ getModelId(m) }}</span>
              </div>
              <div class="col-name" :title="m.filename">
                <span v-if="isBound(m)" class="bound-badge">BOUND</span>
                {{ m.filename }}
              </div>
              <div class="col-type">{{ m.meta?.model_type || 'xgb' }}</div>
              <div class="col-time">{{ formatTime(m.meta?.created_at) }}</div>
              <div class="col-samples">
                <span v-if="m.meta?.n_train">{{ m.meta.n_train }}/{{ m.meta.n_test }}</span>
                <span v-else>—</span>
              </div>
              <div class="col-acc">
                <span v-if="getAccuracy(m)" class="acc-value">{{ getAccuracy(m) }}</span>
                <span v-else>—</span>
              </div>
              <div class="col-actions" @click.stop>
                <button class="action-btn" title="绑定到策略" :disabled="binding || !nodeLabel" @click="onBind(m)">
                  🔗
                </button>
                <button class="action-btn danger" title="删除" @click="onDelete(m)">
                  🗑
                </button>
              </div>
            </div>

            <!-- 展开的详情 -->
            <div v-if="expandedPath === m.path" class="detail-row">
              <ModelDetail :model="m" />
            </div>
          </template>
        </div>
      </div>

      <!-- 对比视图 -->
      <div v-if="compareMode && selectedPaths.size >= 2" class="section">
        <div class="section-heading">
          <div>
            <span class="section-eyebrow">COMPARISON</span>
            <h3 class="section-title">模型对比</h3>
          </div>
        </div>
        <div class="compare-table">
          <div class="compare-header">
            <div class="compare-label">指标</div>
            <div v-for="m in selectedModels" :key="m.path" class="compare-col">
              <span class="compare-model-name">{{ m.filename }}</span>
            </div>
          </div>
          <div v-for="metric in compareMetrics" :key="metric.key" class="compare-row">
            <div class="compare-label">{{ metric.label }}</div>
            <div v-for="m in selectedModels" :key="m.path" class="compare-col">
              <span :class="['compare-value', { best: isBest(metric.key, m) }]">
                {{ getMetric(m, metric.key) }}
              </span>
            </div>
          </div>
        </div>
      </div>
    </template>
  </div>
</template>

<script setup lang="ts">
import { ref, computed, watch, onMounted } from 'vue'
import { ElMessage } from 'element-plus'
import { useMLData } from '../composables/useMLData'
import { useModelBinding } from '../composables/useModelBinding'
import { useHistoryStore, type Strategy } from '../../../stores/history'
import ModelDetail from './ModelDetail.vue'

interface ModelItem {
  path: string
  filename: string
  meta: any
}

const props = defineProps<{
  state: any
  script: string
  selectedStrategyId?: string
  activeTab?: string
}>()

const { listModels, deleteModelFile } = useMLData()
const { binding, bindModel } = useModelBinding()
const strategyStore = useHistoryStore()

const loading = ref(false)
const experiments = ref<ModelItem[]>([])
const production = ref<ModelItem | null>(null)
const expandedPath = ref('')
const compareMode = ref(false)
const selectedPaths = ref(new Set<string>())

const currentStrategyName = computed(() => {
  const id = props.selectedStrategyId
  return strategyStore.strategies.find((s: Strategy) => s.id === id)?.name || ''
})

// 当前策略已绑定的模型文件名集合
const boundFilenames = ref<Set<string>>(new Set())

function refreshBoundLabels() {
  const id = props.selectedStrategyId
  const s = strategyStore.strategies.find((st: Strategy) => st.id === id)
  const models = (s as any)?.data?.models
  const filenames = new Set<string>()
  if (Array.isArray(models)) {
    for (const m of models) {
      if (m?.modelFilename) filenames.add(m.modelFilename)
    }
  }
  boundFilenames.value = filenames
}

function isBound(m: ModelItem): boolean {
  return boundFilenames.value.has(m.filename)
}

const nodeLabel = computed(() => {
  try {
    const graph = JSON.parse(props.script)
    const xn = graph.nodes?.find((n: any) => n.data?.nodeType === 'xgboost')
    return xn?.data?.label || ''
  } catch { return '' }
})

const selectedModels = computed(() =>
  experiments.value.filter(m => selectedPaths.value.has(m.path))
)

const compareMetrics = [
  { key: 'accuracy', label: 'Accuracy' },
  { key: 'f1_macro', label: 'F1 (macro)' },
  { key: 'precision_macro', label: 'Precision' },
  { key: 'recall_macro', label: 'Recall' },
  { key: 'log_loss', label: 'Log Loss' },
  { key: 'n_train', label: '训练样本' },
  { key: 'n_test', label: '测试样本' },
  { key: 'n_features', label: '特征数' },
  { key: 'best_iteration', label: 'Best Iter' },
]

function getModelId(m: ModelItem): string {
  // 从文件名提取时间戳作为唯一 ID：{strategyId}_{YYYYMMDD}_{HHMMSS}.json → MMDD_HHMM
  const match = m.filename.match(/_(\d{4})(\d{2})(\d{2})_(\d{2})(\d{2})(\d{2})\.json$/)
  if (match) return `${match[2]}${match[3]}_${match[4]}${match[5]}`
  return m.meta?.model_id ?? '?'
}

function formatTime(iso?: string): string {
  if (!iso) return '—'
  return iso.replace('T', ' ').slice(0, 16)
}

function getAccuracy(m: ModelItem): string {
  const em = m.meta?.eval_metrics
  if (!em) return ''
  const acc = em.accuracy ?? em.rmse
  if (acc == null) return ''
  return typeof acc === 'number' ? acc.toFixed(4) : String(acc)
}

function getMetric(m: ModelItem, key: string): string {
  if (key === 'n_train' || key === 'n_test' || key === 'n_features' || key === 'best_iteration') {
    const v = m.meta?.[key]
    return v != null ? String(v) : '—'
  }
  const em = m.meta?.eval_metrics
  if (!em || em[key] == null) return '—'
  return typeof em[key] === 'number' ? em[key].toFixed(4) : String(em[key])
}

function isBest(key: string, m: ModelItem): boolean {
  if (key === 'n_train' || key === 'n_test' || key === 'n_features' || key === 'best_iteration') return false
  const vals = selectedModels.value
    .map(x => {
      const v = key.startsWith('n_') || key === 'best_iteration'
        ? x.meta?.[key]
        : x.meta?.eval_metrics?.[key]
      return typeof v === 'number' ? v : null
    })
    .filter((v): v is number => v != null)
  if (vals.length < 2) return false
  const current = key.startsWith('n_') || key === 'best_iteration'
    ? m.meta?.[key]
    : m.meta?.eval_metrics?.[key]
  if (typeof current !== 'number') return false
  const isLowerBetter = key === 'log_loss'
  return isLowerBetter ? current <= Math.min(...vals) : current >= Math.max(...vals)
}

function toggleSelect(path: string) {
  const s = new Set(selectedPaths.value)
  if (s.has(path)) s.delete(path)
  else {
    if (s.size >= 5) {
      ElMessage.warning('最多对比 5 个模型')
      return
    }
    s.add(path)
  }
  selectedPaths.value = s
}

async function refresh() {
  if (!props.selectedStrategyId) return
  loading.value = true
  try {
    const data = await listModels(props.selectedStrategyId)
    if (data) {
      experiments.value = data.experiments || []
      production.value = data.production || null
    }
    refreshBoundLabels()
  } finally {
    loading.value = false
  }
}

async function onBind(m: ModelItem) {
  if (!currentStrategyName.value || !nodeLabel.value) {
    ElMessage.warning('缺少策略名或节点 label')
    return
  }
  const modelId = m.meta?.model_id
  if (!modelId) {
    ElMessage.warning('模型缺少 model_id，无法绑定')
    return
  }
  const ok = await bindModel(modelId, currentStrategyName.value, nodeLabel.value, (bindings) => {
    const id = props.selectedStrategyId
    const idx = strategyStore.strategies.findIndex(s => s.id === id)
    if (idx < 0) return
    const strategy = strategyStore.strategies[idx]
    if (!strategy.data) strategy.data = {}
    const existing: any[] = Array.isArray(strategy.data.models) ? strategy.data.models : []
    for (const b of bindings) {
      // 保存模型文件名，用于精确匹配高亮
      ;(b as any).modelFilename = m.filename
      const i = existing.findIndex(x => x.label === b.label)
      if (i >= 0) existing[i] = b
      else existing.push(b)
      if (strategy.graph?.nodes) {
        for (const n of strategy.graph.nodes) {
          if (n?.data?.nodeType === 'xgboost' && n.data.label === b.label) {
            if (!n.data.params) n.data.params = {}
            n.data.params.modelFile = { value: `production/${strategy.name}-${b.label}.json`, type: 'file' }
          }
        }
      }
    }
    strategy.data.models = existing
    strategyStore.strategies[idx] = strategy
    strategyStore.persistStrategies()
    refreshBoundLabels()
  })
  // 同步更新最新版本 flowData，使 Strategy Chart 加载时能读到 modelFile
  if (ok && props.selectedStrategyId) {
    const id = props.selectedStrategyId
    const versions = strategyStore.getVersionsByStrategy(id)
    if (versions.length > 0) {
      const sorted = [...versions].sort((a, b) =>
        new Date(b.saveTime).getTime() - new Date(a.saveTime).getTime()
      )
      const latest = sorted[0]
      const flowData = await strategyStore.loadVersionFlowData(latest.id)
      if (flowData?.nodes) {
        for (const n of flowData.nodes) {
          if (n?.data?.nodeType === 'xgboost' && n.data.label === nodeLabel.value) {
            if (!n.data.params) n.data.params = {}
            n.data.params.modelFile = { value: `production/${currentStrategyName.value}-${nodeLabel.value}.json`, type: 'file' }
          }
        }
        await strategyStore.saveVersionFlowData(latest.id, flowData)
      }
    }
  }
}

async function onDelete(m: ModelItem) {
  if (!confirm(`确定删除模型 ${m.filename}？\n此操作不可恢复。`)) return
  const ok = await deleteModelFile(m.path)
  if (ok) {
    ElMessage.success('已删除')
    expandedPath.value = ''
    selectedPaths.value.delete(m.path)
    await refresh()
  }
}

watch(() => props.selectedStrategyId, () => {
  expandedPath.value = ''
  selectedPaths.value.clear()
  compareMode.value = false
  refresh()
})

// 训练完成后自动刷新模型列表
watch(() => props.state.trainResult.data, (newData) => {
  if (newData) refresh()
})

// 切到模型管理 tab 时刷新列表
watch(() => props.activeTab, (tab) => {
  if (tab === 'models') refresh()
})

onMounted(() => refresh())
</script>

<style scoped>
.model-manage-panel {
  display: flex;
  flex-direction: column;
  gap: 12px;
  padding: 4px 4px 16px;
}

.toolbar {
  display: flex;
  justify-content: space-between;
  align-items: center;
  gap: 12px;
  padding: 10px 14px;
  background: rgba(15, 25, 41, 0.55);
  border: 1px solid rgba(74, 85, 104, 0.25);
  border-radius: 8px;
}
.toolbar-left { display: flex; align-items: center; gap: 8px; }
.toolbar-right { display: flex; align-items: center; gap: 8px; }
.model-count { font-size: 12px; color: #94a3b8; font-family: 'SF Mono', 'Consolas', monospace; }
.compare-hint { font-size: 12px; color: #5b8ff9; font-family: 'SF Mono', 'Consolas', monospace; }

.btn {
  padding: 5px 14px;
  border-radius: 4px;
  font-size: 12px;
  cursor: pointer;
  border: none;
  transition: all 0.2s;
}
.btn-primary { background: #3b82f6; color: white; }
.btn-primary:hover:not(:disabled) { background: #2563eb; }
.btn-secondary {
  background: rgba(91, 143, 249, 0.12);
  color: #93c5fd;
  border: 1px solid rgba(91, 143, 249, 0.35);
}
.btn-secondary:hover:not(:disabled) { background: rgba(91, 143, 249, 0.2); }
.btn:disabled { opacity: 0.5; cursor: not-allowed; }

.section {
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
.section-title { color: #e2e8f0; font-size: 15px; margin: 4px 0 0; font-weight: 600; }
.section-hint { font-size: 11.5px; color: #94a3b8; }

.production-section { border-color: rgba(38, 166, 91, 0.3); }
.prod-row {
  display: flex;
  align-items: center;
  gap: 12px;
  padding: 8px 12px;
  background: rgba(38, 166, 91, 0.06);
  border-radius: 6px;
  font-size: 12px;
}
.prod-name {
  font-family: 'SF Mono', 'Consolas', monospace;
  color: #e2e8f0;
  flex: 1;
}

.badge {
  display: inline-block;
  font-size: 10px;
  padding: 2px 8px;
  border-radius: 3px;
  font-weight: 600;
  letter-spacing: 1px;
}
.badge.prod { background: rgba(38, 166, 91, 0.18); color: #34d399; }

.meta { font-size: 11px; color: #94a3b8; font-family: 'SF Mono', 'Consolas', monospace; }

.loading-state, .empty-state {
  padding: 24px;
  text-align: center;
  color: #94a3b8;
  font-size: 13px;
}

.model-table { display: flex; flex-direction: column; }

.table-header {
  display: flex;
  align-items: center;
  gap: 0;
  padding: 6px 0;
  border-bottom: 1px solid rgba(74, 85, 104, 0.3);
  font-size: 11px;
  color: #94a3b8;
  font-weight: 500;
}

.table-row {
  display: flex;
  align-items: center;
  gap: 0;
  padding: 8px 0;
  border-bottom: 1px solid rgba(74, 85, 104, 0.1);
  cursor: pointer;
  transition: background 0.15s;
  font-size: 12px;
}
.table-row:hover { background: rgba(74, 85, 104, 0.1); }
.table-row.active { background: rgba(91, 143, 249, 0.08); }
.table-row.bound { background: rgba(38, 166, 91, 0.06); border-left: 3px solid rgba(38, 166, 91, 0.5); }

.bound-badge {
  display: inline-block;
  font-size: 9px;
  padding: 1px 5px;
  border-radius: 3px;
  background: rgba(38, 166, 91, 0.18);
  color: #34d399;
  font-weight: 600;
  letter-spacing: 0.5px;
  margin-right: 6px;
  vertical-align: middle;
}

.col-check { width: 32px; text-align: center; flex-shrink: 0; }
.col-check input { cursor: pointer; accent-color: #3b82f6; }
.col-id { width: 50px; flex-shrink: 0; }
.col-name { flex: 1; min-width: 0; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; font-family: 'SF Mono', 'Consolas', monospace; color: #e2e8f0; font-size: 11.5px; }
.col-type { width: 50px; flex-shrink: 0; color: #94a3b8; font-size: 11px; }
.col-time { width: 120px; flex-shrink: 0; color: #94a3b8; font-family: 'SF Mono', 'Consolas', monospace; font-size: 11px; }
.col-samples { width: 80px; flex-shrink: 0; text-align: right; color: #94a3b8; font-family: 'SF Mono', 'Consolas', monospace; font-size: 11px; }
.col-acc { width: 70px; flex-shrink: 0; text-align: right; }
.col-actions { width: 70px; flex-shrink: 0; display: flex; gap: 4px; justify-content: center; }

.id-pill {
  font-size: 11px;
  padding: 1px 6px;
  border-radius: 3px;
  background: rgba(91, 143, 249, 0.12);
  color: #93c5fd;
  font-family: 'SF Mono', 'Consolas', monospace;
}
.acc-value { color: #34d399; font-family: 'SF Mono', 'Consolas', monospace; font-size: 11.5px; }

.action-btn {
  background: transparent;
  border: none;
  cursor: pointer;
  font-size: 14px;
  padding: 2px 4px;
  border-radius: 4px;
  transition: background 0.15s;
  opacity: 0.7;
}
.action-btn:hover { background: rgba(74, 85, 104, 0.2); opacity: 1; }
.action-btn.danger:hover { background: rgba(234, 57, 67, 0.15); }
.action-btn:disabled { opacity: 0.3; cursor: not-allowed; }

.detail-row {
  border-bottom: 1px solid rgba(74, 85, 104, 0.15);
}

/* 对比表格 */
.compare-table {
  display: flex;
  flex-direction: column;
  font-size: 12px;
}
.compare-header {
  display: flex;
  padding: 8px 0;
  border-bottom: 1px solid rgba(74, 85, 104, 0.3);
  font-weight: 500;
}
.compare-row {
  display: flex;
  padding: 6px 0;
  border-bottom: 1px solid rgba(74, 85, 104, 0.1);
}
.compare-label {
  width: 120px;
  flex-shrink: 0;
  color: #94a3b8;
}
.compare-col {
  flex: 1;
  text-align: center;
  min-width: 0;
}
.compare-model-name {
  font-family: 'SF Mono', 'Consolas', monospace;
  font-size: 11px;
  color: #e2e8f0;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}
.compare-value {
  font-family: 'SF Mono', 'Consolas', monospace;
  color: #cbd5e1;
}
.compare-value.best {
  color: #34d399;
  font-weight: 600;
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
.stage-icon {
  font-size: 28px;
  width: 56px;
  height: 56px;
  display: flex;
  align-items: center;
  justify-content: center;
  background: rgba(91, 143, 249, 0.12);
  border: 1px solid rgba(91, 143, 249, 0.35);
  border-radius: 12px;
  flex-shrink: 0;
}
.stage-empty h3 { margin: 4px 0 6px; font-size: 16px; color: #f1f5f9; font-weight: 600; }
.stage-empty p { margin: 0; color: #94a3b8; font-size: 12.5px; line-height: 1.6; }
</style>
