<template>
  <div class="ml-tab">
    <!-- 顶部：策略选择 + 管线状态轨 -->
    <div class="ml-top">
      <div class="strategy-pick">
        <span class="sp-label">策略</span>
        <select
          class="sp-select"
          :value="selectedStrategyId"
          @change="selectedStrategyId = ($event.target as HTMLSelectElement).value"
        >
          <option value="">请选择策略</option>
          <option v-for="opt in strategyOptions" :key="opt.id" :value="opt.id">{{ opt.name }}</option>
        </select>
      </div>

      <MLStepFlow ref="flowRef" :nodes="flowNodes" @select="goToStep" />
    </div>

    <div v-if="!selectedStrategyId" class="stage-empty stage-empty-hero">
      <div class="stage-icon">00</div>
      <div>
        <h3>在顶部选择策略</h3>
        <p>选一张含 XGBoost 节点的策略，口径、标的与产物会随策略加载。</p>
      </div>
    </div>

    <div v-else-if="strategyLoading" class="stage-empty">
      <div class="stage-icon">…</div>
      <div>
        <h3>正在加载策略</h3>
        <p>读取最新版本的流程图与标的池。</p>
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
      <div class="ml-body">
        <main class="ml-work">
          <!-- 前置未完成：不阻断进入，只提示 -->
          <div v-if="missingSteps.length > 0" class="ready-banner">
            <span class="rb-mark">未就绪</span>
            <div class="rb-text">
              <b>「{{ currentStepLabel }}」的前置步骤尚未完成</b>
              <span>{{ prereqHint }}</span>
            </div>
            <div class="rb-actions">
              <button
                v-for="m in missingSteps"
                :key="m.key"
                class="rb-btn"
                @click="goToStep(m.key)"
              >
                前往「{{ m.label }}」→
              </button>
            </div>
          </div>

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
                <span class="controls-right">
                  <span class="controls-hint">拖动后本地重算</span>
                  <button
                    class="btn-batch btn-compact"
                    :disabled="!canRunLabel || state.labelAnalysis.loading"
                    @click="runLabelAnalysis"
                  >
                    {{ state.labelAnalysis.loading ? '分析中…' : '重新运行分析' }}
                  </button>
                </span>
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

          <div v-show="activeTab === 'models'">
            <ModelManagePanel :state="state" :script="script" :selected-strategy-id="selectedStrategyId" :active-tab="activeTab" />
          </div>
        </main>

        <MLInspector
          :state="state"
          :runs="runHistory"
          :securities="availableSecurities"
          :checked-symbols="checkedSymbols"
          :securities-loading="securitiesLoading"
          :label-symbol-options="labelSymbolOptions"
          @toggle-symbol="toggleSymbol"
          @select-all-symbols="selectAllSymbols"
          @clear-symbols="clearSymbols"
          @goto="goToStep"
        />
      </div>

      <TaskConsole
        title="任务日志"
        :rows="consoleRows"
        :task-label="activeTaskLabel"
        :summary="consoleSummary"
        :collapsed="consoleCollapsed"
        @toggle="consoleCollapsed = !consoleCollapsed"
      />
    </template>
  </div>
</template>

<script setup lang="ts">
import { ref, computed, onMounted, watch, nextTick } from 'vue'
import { useMLState } from './composables/useMLState'
import { useMLData } from './composables/useMLData'
import { useStrategyScript } from './composables/useStrategyScript'
import FeatureAnalysisPanel from './panels/FeatureAnalysisPanel.vue'
import TrainPanel from './panels/TrainPanel.vue'
import ResultPanel from './panels/ResultPanel.vue'
import ModelManagePanel from './panels/ModelManagePanel.vue'
import LabelAnalysisChart from './charts/LabelAnalysisChart.vue'
import MLStepFlow from './MLStepFlow.vue'
import MLInspector from './MLInspector.vue'
import TaskConsole from '@/components/shared/TaskConsole.vue'
import type { FlowNode, SessionRun, ConsoleRow } from '@/types/ml'
import { useStrategySecurities } from '@/components/shared/composables/useStrategySecurities'

const STEPS = [
  { key: 'feature', label: '特征分析', icon: '🔍' },
  { key: 'label', label: '标签分析', icon: '🏷' },
  { key: 'train', label: '训练分析', icon: '⚡' },
  { key: 'result', label: '结果分析', icon: '📊' },
  { key: 'models', label: '模型管理', icon: '📦' },
] as const

type StepKey = typeof STEPS[number]['key']

/**
 * 流程主链：后一步依赖前一步完成。
 * 模型管理是独立面板（浏览/删除模型），不参与主链，任何时候都可进入。
 * 特征门禁已拆成独立分析面板，不在训练链路里。
 */
const STEP_CHAIN: StepKey[] = ['feature', 'label', 'train', 'result']

const STEP_LABEL: Record<string, string> = Object.fromEntries(STEPS.map(s => [s.key, s.label]))

/** 各步骤被锁定时的说明文案 */
const STEP_HINTS: Record<string, string> = {
  label: '标签分析用于确认标签分布是否适合训练。请先完成特征收集，再据此调整预测周期与波动阈值。',
  train: '训练需要「特征分析」产出的特征缓存，以及前面确认过的标签口径。',
  result: '结果分析展示训练产出的模型、评估指标与解释图表，需先完成训练。',
}

const activeTab = ref('feature')
const state = useMLState()
const { field, frequency, dateRange, labelSymbol, setQuickRange } = state
const config = state.config
const { fetchLabelAnalysis, runBatchLabelAnalysis } = useMLData()

// ── 状态轨：步骤状态与前置依赖 ──────────────────────────────

/** 步骤是否已成功完成 */
function stepDone(key: string): boolean {
  switch (key) {
    case 'feature': return !!state.featureReport.data
    case 'label': return !!state.labelAnalysis.result
    case 'train': return !!state.trainResult.data
    case 'result': return !!state.trainResult.data
    default: return false
  }
}

/** 步骤是否已「了结」：当前所有步骤都以成功为准 */
function stepResolved(key: string): boolean {
  return stepDone(key)
}

/** 某步骤缺失的前置步骤 */
function missingFor(key: string): { key: string; label: string }[] {
  const idx = STEP_CHAIN.indexOf(key as StepKey)
  if (idx < 0) return []
  return STEP_CHAIN.slice(0, idx)
    .filter(k => !stepResolved(k))
    .map(k => ({ key: k, label: STEP_LABEL[k] }))
}

/** 当前步骤缺失的前置步骤 */
const missingSteps = computed(() => missingFor(activeTab.value))
const currentStepLabel = computed(() => STEP_LABEL[activeTab.value] || '')
const prereqHint = computed(
  () => STEP_HINTS[activeTab.value] || '前置步骤尚未完成，可先进入查看，但结论可能不完整。',
)

/** 训练指标取值 */
function metric(key: string): string {
  const v = state.trainResult.data?.eval_metrics?.[key]
  return typeof v === 'number' && isFinite(v) ? v.toFixed(3) : ''
}

/** 状态轨节点：序号 + 产物摘要 + 就绪状态 */
const flowNodes = computed<FlowNode[]>(() => STEPS.map((s, i) => {
  const missing = missingFor(s.key)
  const done = stepDone(s.key)
  const failed = false

  let summary = '—'
  let statusText = '未运行'
  switch (s.key) {
    case 'feature': {
      const f = state.featureReport.data
      summary = f ? `${f.total_rows} × ${f.n_features}` : '尚未收集'
      statusText = done ? '已就绪' : '未就绪'
      break
    }
    case 'label': {
      summary = state.labelAnalysis.result ? '已体检' : '尚未体检'
      statusText = done ? '已就绪' : '未就绪'
      break
    }
    case 'train': {
      const tr = state.trainResult.data
      const auc = metric('auc')
      summary = tr ? `#${tr.model_id}${auc ? ` · AUC ${auc}` : ''}` : '尚未训练'
      statusText = done ? '已就绪' : '未就绪'
      break
    }
    case 'result': {
      summary = done ? '训练产物可查看' : '等待训练'
      statusText = done ? '已就绪' : '未就绪'
      break
    }
    default: {
      summary = '实验 / 生产模型'
      statusText = '可进入'
    }
  }

  const title = missing.length > 0
    ? `可进入，但前置未完成：${missing.map(m => m.label).join('、')}`
    : done
      ? `「${s.label}」已完成 · 点击回看`
      : `点击进入「${s.label}」`

  return {
    key: s.key,
    index: String(i + 1).padStart(2, '0'),
    label: s.label,
    summary,
    statusText,
    done,
    failed,
    locked: missing.length > 0,
    active: activeTab.value === s.key,
    title,
  }
}))

/** 点击节点：回跳始终允许；未就绪步骤进入后显示提示条，不拦截 */
function goToStep(key: string) {
  activeTab.value = key
}

// ── 本次会话记录（服务端 Run 历史接入前的本地记录）──────────
const runHistory = ref<SessionRun[]>([])

function clock(): string {
  const d = new Date()
  return `${String(d.getHours()).padStart(2, '0')}:${String(d.getMinutes()).padStart(2, '0')}`
}

function pushRun(stage: SessionRun['stage'], stageLabel: string, detail: string) {
  runHistory.value.unshift({ at: clock(), stage, stageLabel, detail })
  if (runHistory.value.length > 40) runHistory.value.pop()
}

watch(() => state.featureReport.data, (d) => {
  if (d) pushRun('collect', '收集', `${d.total_rows}×${d.n_features}`)
})
watch(() => state.labelAnalysis.result, (r) => {
  if (r) pushRun('label', '标签', `${r.symbol} · ${r.labels.filter((l: number) => l >= 0).length} 样本`)
})
watch(() => state.trainResult.data, (t) => {
  if (t) pushRun('train', '训练', `#${t.model_id}${metric('auc') ? ` · AUC ${metric('auc')}` : ''}`)
})

// ── 任务日志（可折叠）───────────────────────────────────────
const consoleCollapsed = ref(localStorage.getItem('ml-console-collapsed') === '1')
watch(consoleCollapsed, (v) => localStorage.setItem('ml-console-collapsed', v ? '1' : '0'))

const activeTask = computed<'collect' | 'train' | ''>(() => {
  if (state.trainResult.loading) return 'train'
  if (state.featureReport.loading) return 'collect'
  if (state.trainResult.steps.length) return 'train'
  if (state.featureReport.steps.length) return 'collect'
  return ''
})

const activeTaskLabel = computed(() => ({
  collect: '特征收集',
  train: '训练分析',
  '': '空闲',
}[activeTask.value]))

const STEP_ICON: Record<string, string> = {
  done: '✓', running: '⏳', error: '✗', pending: '○',
}

const consoleRows = computed<ConsoleRow[]>(() => {
  const rows: ConsoleRow[] = []
  const kind = activeTask.value
  const src: any =
    kind === 'train' ? state.trainResult
      : kind === 'collect' ? state.featureReport
        : null
  if (!src) return rows

  for (const s of src.steps as Array<{ id: string; label: string; status: string; detail?: string }>) {
    rows.push({
      level: s.status === 'error' ? 'error' : s.status === 'done' ? 'done' : 'step',
      tag: STEP_ICON[s.status] || '○',
      text: `${s.label}${s.detail ? ` · ${s.detail}` : ''}`,
    })
  }
  for (const l of src.logs as Array<{ level: string; line: string }>) {
    rows.push({
      level: l.level === 'error' ? 'error' : l.level === 'warning' ? 'warn' : 'info',
      tag: '日志',
      text: l.line,
    })
  }
  return rows
})

const consoleSummary = computed(() => {
  const kind = activeTask.value
  if (!kind) return ''
  const running =
    kind === 'train' ? state.trainResult.loading : state.featureReport.loading
  return running ? '运行中…' : '已完成'
})

const {
  strategyOptions,
  selectedStrategyId,
  availableSecurities,
  checkedSymbols,
  loading: securitiesLoading,
  loadSecuritiesForStrategy,
  toggleSymbol,
} = useStrategySecurities({ defaultCheckAll: true })

const ss = useStrategyScript()
const { script, hasMLNode, loading: strategyLoading } = ss

// ── 诊断：定位「流程图不显示」 ────────────────────────────────
const flowRef = ref<any>(null)

function logFlowDiag(reason: string) {
  const el = flowRef.value?.$el as HTMLElement | undefined
  const rect = el?.getBoundingClientRect()
  console.log('[MLFlow] diag', JSON.stringify({
    reason,
    activeTab: activeTab.value,
    hasMLNode: hasMLNode.value,
    strategy: selectedStrategyId.value,
    viewport: `${window.innerWidth}x${window.innerHeight}`,
    mounted: !!el,
    rect: rect ? { w: Math.round(rect.width), h: Math.round(rect.height) } : null,
    nodes: flowNodes.value.map(n => `${n.label}${n.done ? '[done]' : ''}${n.failed ? '[fail]' : ''}${n.locked ? '[lock]' : ''}${n.active ? '[active]' : ''}`),
    missing: missingSteps.value.map(m => m.key),
    chain: {
      feature: !!state.featureReport.data,
      label: !!state.labelAnalysis.result,
      train: !!state.trainResult.data,
    },
  }))
}

watch(activeTab, () => logFlowDiag('tab-change'))
watch(flowNodes, () => logFlowDiag('state-change'))
onMounted(() => {
  nextTick(() => requestAnimationFrame(() => logFlowDiag('mounted')))
})

// 标签标的选项（从已勾选标的中取）
const labelSymbolOptions = computed(() => Array.from(checkedSymbols.value))

// 是否可以运行标签分析
const canRunLabel = computed(() => {
  if (!selectedStrategyId.value || checkedSymbols.value.size === 0 || !dateRange.value) return false
  if (config.labelShape === 'vector') return !!labelSymbol.value
  return true  // matrix 模式不需要选择标签标的
})

// 标的全选 / 清空（Inspector 内操作）
function selectAllSymbols() {
  checkedSymbols.value = new Set(availableSecurities.value.map(s => s.code))
}

function clearSymbols() {
  checkedSymbols.value = new Set()
}

async function onStrategyChange() {
  if (!selectedStrategyId.value) {
    ss.reset()
    return
  }

  // 产物与结论都属于上一张策略，切策略时一并作废，避免 Inspector 展示错位
  state.reset()
  runHistory.value = []
  loadSecuritiesForStrategy(selectedStrategyId.value)

  const res = await ss.load(selectedStrategyId.value)
  if (!res) return

  // 清空旧 labelSource，避免发往后端的 symbol 指向已删除标的；
  // TrainPanel 的 watch 会在 script 变化后用新策略的首位 code 重新填充
  state.config.labelSource = ''

  // 从策略的回测配置初始化日期范围（优先 backtest 字段，兜底"全部数据"）
  if (res.backtest?.start && res.backtest?.end) {
    dateRange.value = [res.backtest.start, res.backtest.end]
  } else {
    // 未配置回测时间 → 使用数据全范围（传空字符串让后端自行判断）
    dateRange.value = ['', '']
  }
  setQuickRange('')
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
  background: #0a0e13;
  color: #dbe4ee;
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

/* ── 顶部：策略选择 + 状态轨 ────────────────── */
.ml-top {
  flex: 0 0 auto;
  display: flex;
  align-items: stretch;
  background: #0b1015;
  border-bottom: 1px solid rgba(74, 85, 104, 0.25);
}

.strategy-pick {
  flex: 0 0 auto;
  display: flex;
  flex-direction: column;
  justify-content: center;
  gap: 3px;
  padding: 8px 14px;
  border-right: 1px solid rgba(74, 85, 104, 0.22);
}

.sp-label {
  font-size: 9.5px;
  letter-spacing: 1.4px;
  color: #7d8c9e;
  font-weight: 600;
}

.sp-select {
  min-width: 186px;
  padding: 4px 7px;
  background: #0f161e;
  border: 1px solid rgba(74, 85, 104, 0.4);
  border-radius: 3px;
  color: #dbe4ee;
  font-size: 11.5px;
  outline: none;
}

.sp-select:focus { border-color: #38bdf8; }

/* ── 工作台布局：主工作区 + Inspector ────────── */
.ml-body {
  flex: 1;
  min-height: 0;
  display: grid;
  grid-template-columns: minmax(0, 1fr) 316px;
}

.ml-work {
  min-width: 0;
  min-height: 0;
  overflow: auto;
  background: #0d1218;
}

.ml-work::-webkit-scrollbar { width: 6px; }
.ml-work::-webkit-scrollbar-thumb { background: #22303f; }

/* 未就绪提示：不阻断进入，只在工作区顶部提示 */
.ready-banner {
  display: flex;
  align-items: center;
  gap: 12px;
  margin: 12px 16px 0;
  padding: 9px 12px;
  border: 1px solid rgba(251, 191, 36, 0.32);
  border-left-width: 2px;
  border-radius: 4px;
  background: rgba(251, 191, 36, 0.05);
}

.rb-mark {
  flex: 0 0 auto;
  padding: 1px 6px;
  border-radius: 2px;
  background: rgba(251, 191, 36, 0.16);
  color: #fcd34d;
  font-size: 9.5px;
  letter-spacing: 0.6px;
}

.rb-text { min-width: 0; }

.rb-text b {
  display: block;
  font-size: 11.5px;
  font-weight: 600;
  color: #d6bd88;
}

.rb-text span {
  display: block;
  margin-top: 1px;
  font-size: 10.5px;
  color: #8a7f66;
}

.rb-actions {
  margin-left: auto;
  display: flex;
  gap: 6px;
  flex: 0 0 auto;
}

.rb-btn {
  padding: 4px 10px;
  background: rgba(251, 191, 36, 0.1);
  border: 1px solid rgba(251, 191, 36, 0.34);
  border-radius: 3px;
  color: #e8c583;
  font-family: inherit;
  font-size: 10.5px;
  cursor: pointer;
  white-space: nowrap;
}

.rb-btn:hover { background: rgba(251, 191, 36, 0.18); }

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
.controls-right {
  display: flex;
  align-items: center;
  gap: 10px;
}
.btn-compact {
  padding: 3px 10px;
  font-size: 11px;
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
</style>
