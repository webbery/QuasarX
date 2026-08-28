<template>
  <div class="train-panel">
    <!-- 训练入口 -->
    <div class="train-summary">
      <div class="summary-text">
        <span class="section-eyebrow">TRAINING INPUT</span>
        <h3>调整训练参数并启动训练</h3>
        <p>基础参数覆盖 80% 的调参场景；高级正则项在折叠区内，通常只在过拟合/欠拟合时调整。</p>
      </div>
      <div class="summary-action">
        <button class="btn btn-primary" :disabled="state.trainResult.loading || (config.labelShape === 'vector' && !config.labelSource) || !state.featureReport.data" @click="onTrain()">
          <i v-if="state.trainResult.loading" class="fas fa-spinner fa-spin"></i>
          {{ state.trainResult.loading ? '训练中…' : '开始训练' }}
        </button>
        <span v-if="!state.featureReport.data" class="source-chip warn">
          <i class="fas fa-exclamation-circle"></i>请先完成特征分析
        </span>
        <span v-else-if="config.labelShape === 'matrix'" class="source-chip">
          <i class="fas fa-th"></i>多标签矩阵 (per-symbol)
        </span>
        <span v-else-if="config.labelSource" class="source-chip" :title="`标签来源：${config.labelSource}`">
          <i class="fas fa-tag"></i>{{ config.labelSource }}
        </span>
        <span v-else class="source-chip warn">
          <i class="fas fa-exclamation-circle"></i>未选择标签来源
        </span>
      </div>
    </div>

    <!-- 训练进度 -->
    <div v-if="state.trainResult.steps.length > 0" class="training-progress">
      <div class="progress-header">
        <span class="progress-title">训练进度</span>
        <span v-if="!state.trainResult.loading" class="progress-done">完成</span>
      </div>
      <div class="step-list">
        <div v-for="step in state.trainResult.steps" :key="step.id" class="step-item">
          <span class="step-icon" :class="step.status">
            <template v-if="step.status === 'done'">✓</template>
            <template v-else-if="step.status === 'running'">⏳</template>
            <template v-else-if="step.status === 'error'">✗</template>
            <template v-else>○</template>
          </span>
          <span class="step-label">{{ step.label }}</span>
          <div v-if="step.status === 'running' && step.detail && step.detail.includes('/')" class="step-progress-inline">
            <div class="step-progress-bar">
              <div class="step-progress-fill" :style="{ width: parseProgressPct(step.detail) + '%' }"></div>
            </div>
            <span class="step-progress-pct">{{ parseProgressPct(step.detail) }}%</span>
          </div>
          <span v-else-if="step.detail" class="step-detail">{{ step.detail }}</span>
          <span v-if="step.elapsedMs" class="step-time">{{ step.elapsedMs }}ms</span>
        </div>
        <div v-for="(log, i) in state.trainResult.logs" :key="'log-'+i" class="log-item" :class="log.level">
          <span class="log-icon">{{ log.level === 'warning' ? '⚠' : log.level === 'error' ? '✗' : 'ℹ' }}</span>
          <span class="log-line">{{ log.line }}</span>
        </div>
      </div>
    </div>

    <!-- 参数优化 -->
    <div class="optimize-section">
      <div class="optimize-header" @click="showOptimize = !showOptimize">
        <div>
          <span class="section-eyebrow">OPTUNA TPE</span>
          <h3 class="section-title">参数优化</h3>
        </div>
        <span class="collapse-arrow" :class="{ open: showOptimize }">▶</span>
      </div>

      <div v-if="showOptimize" class="optimize-body">
        <!-- 配置行 -->
        <div class="optimize-config">
          <div class="config-item">
            <label>优化指标</label>
            <select v-model="optimizeMetric" class="select-input">
              <option v-for="m in METRIC_OPTIONS" :key="m.value" :value="m.value">{{ m.label }}</option>
            </select>
          </div>
          <div class="config-item">
            <label>试验次数</label>
            <input type="number" v-model.number="nTrials" min="5" max="500" step="5" class="select-input" />
          </div>
          <div class="config-item optimize-actions">
            <button class="btn btn-primary" :disabled="optimizeRunning || !state.featureReport.data" @click="onOptimize()">
              <i v-if="optimizeRunning" class="fas fa-spinner fa-spin"></i>
              {{ optimizeRunning ? '优化中…' : '开始优化' }}
            </button>
            <button class="btn btn-ghost" :disabled="optimizeRunning" @click="resetDomains()">重置默认域</button>
          </div>
        </div>

        <!-- 参数搜索域表格 -->
        <div class="domain-table">
          <div class="domain-row domain-header">
            <span class="domain-col domain-enable">启用</span>
            <span class="domain-col domain-name">参数</span>
            <span class="domain-col domain-min">最小值</span>
            <span class="domain-col domain-max">最大值</span>
            <span class="domain-col domain-step">步长</span>
            <span class="domain-col domain-log">Log</span>
          </div>
          <div v-for="(d, key) in paramDomains" :key="key" class="domain-row">
            <span class="domain-col domain-enable">
              <input type="checkbox" v-model="d.enabled" />
            </span>
            <span class="domain-col domain-name">{{ PARAM_LABELS[key] || key }}</span>
            <span class="domain-col domain-min">
              <input type="number" v-model.number="d.min" :step="d.step || (d.log ? 0.005 : 0.1)" class="domain-input" />
            </span>
            <span class="domain-col domain-max">
              <input type="number" v-model.number="d.max" :step="d.step || (d.log ? 0.005 : 0.1)" class="domain-input" />
            </span>
            <span class="domain-col domain-step">
              <input v-if="d.step != null" type="number" v-model.number="d.step" min="1" class="domain-input" />
              <span v-else class="domain-na">—</span>
            </span>
            <span class="domain-col domain-log">
              <input v-if="d.log != null" type="checkbox" v-model="d.log" />
              <span v-else class="domain-na">—</span>
            </span>
          </div>
        </div>

        <!-- 进度 -->
        <div v-if="optimizeRunning || optimizeTrials.length > 0" class="optimize-progress">
          <div class="progress-bar">
            <div class="progress-fill" :style="{ width: (optimizeTrials.length / nTrials * 100) + '%' }"></div>
          </div>
          <span class="progress-text">{{ optimizeProgress }}</span>
        </div>

        <!-- 结果 -->
        <div v-if="optimizeResult" class="optimize-result">
          <div class="result-header">
            <div class="result-summary">
              <span class="result-badge">最佳 {{ optimizeResult.metric }}</span>
              <span class="result-value">{{ optimizeResult.best_value.toFixed(4) }}</span>
              <span class="result-trial">trial #{{ optimizeResult.best_trial_number }}</span>
              <span class="result-duration">{{ (optimizeResult.optimization_duration_ms / 1000).toFixed(1) }}s</span>
            </div>
            <button class="btn btn-accent" @click="state.applyBestParams()">
              <i class="fas fa-arrow-down"></i>应用最佳参数
            </button>
          </div>

          <div class="result-charts">
            <div class="chart-box">
              <h4>优化历史</h4>
              <OptimizationHistoryChart :trials="optimizeResult.trials" :metric="optimizeResult.metric" :height="220" />
            </div>
            <div class="chart-box" v-if="optimizeResult.importance?.length">
              <h4>参数重要性</h4>
              <ParameterImportanceChart :importance="optimizeResult.importance" :height="220" />
            </div>
          </div>

          <!-- Trial 对比表 -->
          <details class="trials-detail">
            <summary>全部 {{ optimizeResult.trials.length }} 次试验</summary>
            <div class="trials-table-wrap">
              <table class="trials-table">
                <thead>
                  <tr>
                    <th>#</th>
                    <th>{{ optimizeResult.metric }}</th>
                    <th v-for="(d, key) in optimizeResult.best_params" :key="key">{{ PARAM_LABELS[key] || key }}</th>
                    <th>耗时</th>
                  </tr>
                </thead>
                <tbody>
                  <tr v-for="t in sortedTrials" :key="t.number" :class="{ 'best-row': t.number === optimizeResult.best_trial_number, 'failed-row': t.status === 'failed' }">
                    <td>{{ t.number }}</td>
                    <td>{{ t.value != null ? t.value.toFixed(4) : '—' }}</td>
                    <td v-for="(v, key) in optimizeResult.best_params" :key="key">
                      {{ t.params[key] != null ? (typeof t.params[key] === 'number' && t.params[key] % 1 !== 0 ? t.params[key].toFixed(4) : t.params[key]) : '—' }}
                    </td>
                    <td>{{ (t.duration_ms / 1000).toFixed(1) }}s</td>
                  </tr>
                </tbody>
              </table>
            </div>
          </details>
        </div>
      </div>
    </div>

    <!-- 基础参数 -->
    <div class="config-section">
      <div class="section-heading">
        <div>
          <span class="section-eyebrow">CORE PARAMS</span>
          <h3 class="section-title">标签与目标</h3>
        </div>
      </div>
      <div class="config-grid">
        <div class="config-item">
          <label>标签类型</label>
          <select v-model="config.labelType" class="select-input" @change="onLabelTypeChange">
            <option v-for="t in LABEL_TYPES" :key="t.value" :value="t.value">{{ t.label }}</option>
          </select>
        </div>
        <div class="config-item">
          <label>
            标签形状
            <span class="tip-icon" title="定义标签与标的的对应关系。&#10;多标签矩阵：每个标的根据自身价格独立计算涨/跌/平标签，样本数=N×K，特征匿名化拼接。&#10;单标签向量：所有标的共享一个标签（基于 labelSource），样本数=N。">?</span>
          </label>
          <select v-model="config.labelShape" class="select-input">
            <option v-for="s in LABEL_SHAPES" :key="s.value" :value="s.value">{{ s.label }}</option>
          </select>
          <span class="config-hint">{{ labelShapeDesc }}</span>
        </div>
        <div class="config-item">
          <label>预测周期 N (天)</label>
          <input type="number" v-model.number="config.labelPeriod" min="1" max="60" class="select-input" />
        </div>
        <div class="config-item">
          <label>
            阈值系数 vol_k
            <span class="tip-icon" title="自适应阈值 = vol_k × σ × √N，σ 为对数收益率标准差">?</span>
          </label>
          <input type="number" v-model.number="config.volK" step="0.1" min="0.1" max="2.0" class="select-input"
                 :disabled="config.labelType !== 'classification'" />
          <span class="config-hint">threshold = vol_k × σ × √N</span>
        </div>
        <div class="config-item">
          <label>目标函数</label>
          <input :value="config.objective" disabled class="select-input readonly-input" />
          <span class="config-hint">由标签类型自动推导</span>
        </div>
      </div>
    </div>

    <div class="config-section">
      <div class="section-heading">
        <div>
          <span class="section-eyebrow">CORE PARAMS</span>
          <h3 class="section-title">训练核心</h3>
        </div>
      </div>
      <div class="config-grid">
        <div class="config-item">
          <label>
            学习率
            <span class="tip-icon" title="每棵树的步长，越小越稳定但需更多迭代。典型值 0.01~0.3">?</span>
          </label>
          <input type="number" v-model.number="config.learningRate" step="0.01" min="0.001" max="1.0" class="select-input" />
        </div>
        <div class="config-item">
          <label>
            最大深度
            <span class="tip-icon" title="单棵树的最大深度，控制模型复杂度。越大越容易过拟合">?</span>
          </label>
          <input type="number" v-model.number="config.maxDepth" min="1" max="20" class="select-input" />
        </div>
        <div class="config-item">
          <label>迭代次数</label>
          <input type="number" v-model.number="config.nEstimators" min="10" max="2000" class="select-input" />
        </div>
        <div class="config-item">
          <label>
            早停轮数
            <span class="tip-icon" title="验证集损失连续 N 轮不下降则停止训练，防止过拟合">?</span>
          </label>
          <input type="number" v-model.number="config.earlyStoppingRounds" min="1" max="100" class="select-input" />
        </div>
        <div class="config-item">
          <label>
            训练集比例
            <span class="tip-icon" title="自动推导：1 - 验证集比例 - 测试集比例。数据量 ≥ 1000 行时三段划分，否则验证集归零退化为两段">?</span>
          </label>
          <input :value="trainRatioDisplay" disabled class="select-input readonly-input" />
          <span class="config-hint">{{ trainRatioHint }}</span>
        </div>
        <div class="config-item">
          <label>
            验证集比例
            <span class="tip-icon" title="用于早停（early stopping），不参与最终评估。设为 0 则退化为两段划分">?</span>
          </label>
          <input type="number" v-model.number="config.valRatio" step="0.05" min="0" max="0.5" class="select-input" />
        </div>
        <div class="config-item">
          <label>
            测试集比例
            <span class="tip-icon" title="最终评估集，不参与训练过程">?</span>
          </label>
          <input type="number" v-model.number="config.testRatio" step="0.05" min="0.05" max="0.5" class="select-input" />
        </div>
      </div>
    </div>

    <!-- 高级参数 -->
    <div class="config-section">
      <button class="advanced-toggle" @click="showAdvanced = !showAdvanced">
        <i :class="showAdvanced ? 'fas fa-chevron-down' : 'fas fa-chevron-right'"></i>
        采样与正则化（高级）
        <span class="advanced-hint">仅在过拟合/欠拟合或类别不平衡时展开</span>
      </button>
      <div v-show="showAdvanced" class="advanced-body">
        <div class="config-grid">
          <div class="config-item">
            <label>
              行采样 subsample
              <span class="tip-icon" title="每棵树训练时随机抽取的样本比例。降低可减少过拟合，典型值 0.5~1.0">?</span>
            </label>
            <input type="number" v-model.number="config.subsample" step="0.05" min="0.1" max="1.0" class="select-input" />
          </div>
          <div class="config-item">
            <label>
              列采样 colsample_bytree
              <span class="tip-icon" title="每棵树训练时随机抽取的特征比例。降低可增加模型多样性">?</span>
            </label>
            <input type="number" v-model.number="config.colsampleBytree" step="0.05" min="0.1" max="1.0" class="select-input" />
          </div>
          <div class="config-item">
            <label>
              正则化
              <span class="tip-icon" title="L1(Lasso)促进特征选择，L2(Ridge)抑制极端权重">?</span>
            </label>
            <select v-model="config.regMode" class="select-input">
              <option v-for="m in REG_MODES" :key="m.value" :value="m.value">{{ m.label }}</option>
            </select>
          </div>
          <div class="config-item">
            <label>
              正则化强度
              <span class="tip-icon" title="正则化系数，值越大约束越强。典型值 0~10">?</span>
            </label>
            <input type="number" v-model.number="config.regValue" step="0.1" min="0" max="100" class="select-input"
                   :disabled="config.regMode === 'none'" />
            <span class="config-hint">{{ regHint }}</span>
          </div>
          <div class="config-item">
            <label>
              gamma
              <span class="tip-icon" title="分裂所需的最小增益，值越大越保守，减少不必要的分裂">?</span>
            </label>
            <input type="number" v-model.number="config.gamma" step="0.1" min="0" max="10" class="select-input" />
          </div>
          <div class="config-item">
            <label>
              min_child_weight
              <span class="tip-icon" title="叶节点的最小样本权重和，值越大越保守，减少过拟合">?</span>
            </label>
            <input type="number" v-model.number="config.minChildWeight" min="1" max="100" class="select-input" />
          </div>
          <div class="config-item">
            <label>
              scale_pos_weight
              <span class="tip-icon" title="正样本权重倍数，用于类别不平衡。设为 负类数/正类数 可平衡分类">?</span>
            </label>
            <input type="number" v-model.number="config.scalePosWeight" step="0.1" min="0.1" max="100" class="select-input" />
          </div>
        </div>
      </div>
    </div>

    <!-- 训练结果摘要 -->
    <div v-if="state.trainResult.data" class="result-summary">
      <div class="result-stat">
        <span class="result-label">模型 ID</span>
        <span class="result-value">#{{ state.trainResult.data.model_id }}</span>
      </div>
      <div class="result-stat">
        <span class="result-label">模型类型</span>
        <span class="result-value">{{ state.trainResult.data.model_type || 'xgboost' }}</span>
      </div>
      <div class="result-stat">
        <span class="result-label">{{ state.trainResult.data.n_val > 0 ? '训练 / 验证 / 测试' : '训练 / 测试' }}</span>
        <span class="result-value">{{ state.trainResult.data.n_train }}{{ state.trainResult.data.n_val > 0 ? ` / ${state.trainResult.data.n_val}` : '' }} / {{ state.trainResult.data.n_test }}</span>
      </div>
      <div class="result-stat">
        <span class="result-label">特征数</span>
        <span class="result-value">{{ state.trainResult.data.n_features }}</span>
      </div>
      <div class="result-stat">
        <span class="result-label">最佳迭代</span>
        <span class="result-value">{{ state.trainResult.data.best_iteration }}</span>
      </div>
    </div>

    <!-- Loss 曲线 -->
    <div v-if="chartData.length > 0" class="chart-section">
      <div class="section-heading">
        <div>
          <span class="section-eyebrow">LOSS CURVE</span>
          <h3 class="section-title">Loss 曲线</h3>
        </div>
        <span class="section-hint">{{ state.trainResult.loading ? '实时更新中…' : '训练/验证损失随迭代变化，虚线为早停触发点' }}</span>
      </div>
      <LearningCurveChart
        :data="chartData"
        :best-iteration="state.trainResult.data?.best_iteration"
        :objective="config.objective"
      />
    </div>

    <div v-if="!state.trainResult.data && !state.trainResult.loading" class="empty-state">
      <div class="empty-icon">⚡</div>
      <div v-if="!state.featureReport.data">请先在"特征分析"面板完成数据收集</div>
      <div v-else>确认标签来源并配置训练参数后点击"开始训练"</div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { computed, ref, watch } from 'vue'
import { LABEL_TYPES, LABEL_SHAPES, REG_MODES } from '../composables/useMLState'
import type { TrainStep, TrainLog, LearningCurvePoint } from '../composables/useMLState'
import { useMLData } from '../composables/useMLData'
import LearningCurveChart from '../charts/LearningCurveChart.vue'
import OptimizationHistoryChart from '../charts/OptimizationHistoryChart.vue'
import ParameterImportanceChart from '../charts/ParameterImportanceChart.vue'

const showAdvanced = ref(false)
const showOptimize = ref(false)

const props = defineProps<{
  state: any
  script: string
}>()

const emit = defineEmits<{
  (e: 'trained'): void
}>()

const { train, optimize } = useMLData()

const config = props.state.config

// 优化相关状态（从 state 解构）
const optimizeResult = computed(() => props.state.optimizeResult)
const optimizeRunning = computed(() => props.state.optimizeRunning)
const optimizeTrials = computed(() => props.state.optimizeTrials)
const optimizeProgress = computed(() => props.state.optimizeProgress)
const paramDomains = computed(() => props.state.paramDomains)
const optimizeMetric = computed({
  get: () => props.state.optimizeMetric,
  set: (v: string) => { props.state.optimizeMetric = v },
})
const nTrials = computed({
  get: () => props.state.nTrials,
  set: (v: number) => { props.state.nTrials = v },
})

const METRIC_OPTIONS = [
  { value: 'sharpe', label: 'Sharpe Ratio' },
  { value: 'total_return', label: 'Total Return' },
  { value: 'annual_return', label: 'Annual Return' },
  { value: 'max_drawdown', label: 'Max Drawdown' },
  { value: 'win_rate', label: 'Win Rate' },
  { value: 'calmar_ratio', label: 'Calmar Ratio' },
]

const PARAM_LABELS: Record<string, string> = {
  learning_rate: '学习率',
  max_depth: '最大深度',
  n_estimators: '迭代次数',
  subsample: '行采样',
  colsample_bytree: '列采样',
  gamma: '最小增益',
  min_child_weight: '叶节点权重',
  reg_alpha: 'L1 正则',
  reg_lambda: 'L2 正则',
}

const liveLearningCurve = ref<LearningCurvePoint[]>([])
// 独立缓存：SSE log 推过来的实时 loss 点全量保留,
// 不受 trainResult.data?.learning_curve 是否完整的影响。
// 切到 [结果分析] Tab 再切回时,作为 fallback 兜底显示 loss 曲线。
const cachedLearningCurve = ref<LearningCurvePoint[]>([])
const chartData = computed(() => {
  if (props.state.trainResult.data?.learning_curve?.length) {
    return props.state.trainResult.data.learning_curve
  }
  if (cachedLearningCurve.value.length) {
    return cachedLearningCurve.value
  }
  return liveLearningCurve.value
})

const trainRatioDisplay = computed(() => {
  const r = Math.max(0, 1 - config.valRatio - config.testRatio)
  return r.toFixed(2)
})
const trainRatioHint = computed(() => {
  const total = config.valRatio + config.testRatio
  if (total >= 1) return '⚠ 验证+测试 ≥ 1'
  return '1 - val - test'
})

const regHint = computed(() => {
  if (config.regMode === 'none') return '不施加正则化'
  if (config.regMode === 'l1') return `reg_alpha = ${config.regValue}`
  if (config.regMode === 'l2') return `reg_lambda = ${config.regValue}`
  return `reg_alpha = ${config.regValue}, reg_lambda = ${config.regValue}`
})

const sortedTrials = computed(() => {
  if (!optimizeResult.value?.trials) return []
  return [...optimizeResult.value.trials]
    .filter(t => t.status === 'ok' && t.value != null)
    .sort((a, b) => (b.value ?? 0) - (a.value ?? 0))
})

const labelShapeDesc = computed(() => {
  if (config.labelShape === 'matrix') return '每个标的独立标签，样本数 = N×K'
  return '共享标签（基于 labelSource），样本数 = N'
})

watch(() => props.script, (s) => {
  if (s && !config.labelSource) {
    try {
      const parsed = JSON.parse(s)
      for (const node of (parsed.nodes || [])) {
        if (node.data?.nodeType === 'input') {
          const params = node.data.params || {}
          const codeParam = params['code'] || params['代码']
          if (codeParam?.value) {
            let codes: string[]
            if (Array.isArray(codeParam.value)) {
              codes = codeParam.value
            } else if (typeof codeParam.value === 'string' && codeParam.value.includes(',')) {
              codes = codeParam.value.split(',').map((s: string) => s.trim()).filter(Boolean)
            } else {
              codes = [String(codeParam.value)]
            }
            if (codes.length > 0) {
              config.labelSource = `${codes[0]}.close`
              break
            }
          }
        }
      }
    } catch { /* ignore */ }
  }
}, { immediate: true })

// 从策略 JSON 中提取首个 XGBoostNode 的 label 作为 bind 时的 production 文件名后缀
function extractXgbNodeLabel(script: string): string | undefined {
  try {
    const parsed = JSON.parse(script)
    const nodes = parsed?.graph?.nodes ?? parsed?.nodes
    if (!Array.isArray(nodes)) return undefined
    for (const n of nodes) {
      if (n?.data?.nodeType === 'xgboost' && typeof n.data.label === 'string' && n.data.label.trim()) {
        return n.data.label.trim()
      }
    }
  } catch { /* ignore */ }
  return undefined
}

function onLabelTypeChange() {
  props.state.syncObjective()
}

const STEP_LABELS: Record<string, string> = {
  parse_script: '解析策略图',
  init_nodes: '初始化节点',
  start_exchange: '启动数据源',
  collect_data: '收集特征数据',
  analyze: '分析特征数据',
  train_model: 'Python 训练',
}

function parseProgressPct(detail: string): number {
  const m = detail.match(/(\d+)\s*\/\s*(\d+)/)
  if (m && Number(m[2]) > 0) return Math.round(Number(m[1]) / Number(m[2]) * 100)
  return 0
}

async function onTrain() {
  props.state.trainResult.loading = true
  props.state.trainResult.progressMsg = '开始训练...'
  props.state.trainResult.steps = []
  props.state.trainResult.logs = []
  liveLearningCurve.value = []
  cachedLearningCurve.value = []

  const stepStartTimes: Record<string, number> = {}

  let regAlpha = 0, regLambda = 0
  if (config.regMode === 'l1') regAlpha = config.regValue
  else if (config.regMode === 'l2') regLambda = config.regValue
  else if (config.regMode === 'both') { regAlpha = config.regValue; regLambda = config.regValue }

  try {
    const result = await train(props.script, {
      labelSource: config.labelSource,
      labelPeriod: config.labelPeriod,
      labelType: config.labelType,
      labelShape: config.labelShape,
      volK: config.volK,
      objective: config.objective,
      numClass: config.numClass,
      testRatio: config.testRatio,
      valRatio: config.valRatio,
      learningRate: config.learningRate,
      maxDepth: config.maxDepth,
      nEstimators: config.nEstimators,
      earlyStoppingRounds: config.earlyStoppingRounds,
      subsample: config.subsample,
      colsampleBytree: config.colsampleBytree,
      gamma: config.gamma,
      minChildWeight: config.minChildWeight,
      scalePosWeight: config.scalePosWeight,
      regAlpha,
      regLambda,
      startDate: props.state.dateRange.value?.[0] || '',
      endDate: props.state.dateRange.value?.[1] || '',
      frequency: props.state.frequency.value || '1d',
    }, (type: string, data: any) => {
      if (type === 'step') {
        const stepId = data.step
        const existing = props.state.trainResult.steps.find((s: TrainStep) => s.id === stepId)
        if (data.status === 'start') {
          if (existing) {
            existing.status = 'running'
            existing.detail = data.msg
            existing.elapsedMs = undefined
          } else {
            stepStartTimes[stepId] = Date.now()
            props.state.trainResult.steps.push({
              id: stepId,
              label: STEP_LABELS[stepId] || stepId,
              status: 'running',
              detail: data.msg,
            })
          }
        } else if (data.status === 'done') {
          if (existing) {
            existing.status = 'done'
            existing.elapsedMs = Date.now() - (stepStartTimes[stepId] || 0)
            if (data.features) existing.detail = `${data.features} 个特征`
            else if (data.bars) existing.detail = `${data.bars} bars`
            else if (data.symbols) existing.detail = `${data.symbols} 个标的`
          }
        }
      } else if (type === 'progress') {
        const step = props.state.trainResult.steps.find((s: TrainStep) => s.id === data.step)
        if (step && data.total > 0) {
          step.detail = `${data.current} / ${data.total} epochs`
        }
      } else if (type === 'warning') {
        const line = data.msg || data.line || ''
        if (!props.state.trainResult.logs.some((l: TrainLog) => l.line === line && l.level === 'warning')) {
          props.state.trainResult.logs.push({ step: data.step || '', level: 'warning', line })
        }
      } else if (type === 'log') {
        props.state.trainResult.logs.push({ step: data.step || '', level: 'info', line: data.line || '' })
        try {
          const logData = JSON.parse(data.line)
          if (logData.phase === 'training' && logData.iteration != null && logData.train_loss != null) {
            const point: LearningCurvePoint = {
              iteration: logData.iteration,
              train_loss: logData.train_loss,
              eval_loss: logData.eval_loss ?? logData.train_loss,
            }
            liveLearningCurve.value.push(point)
            cachedLearningCurve.value.push(point)
          }
        } catch { /* not JSON or not training progress */ }
      } else if (type === 'error') {
        const step = props.state.trainResult.steps.find((s: TrainStep) => s.id === data.step)
        if (step) step.status = 'error'
        const line = data.msg || ''
        if (!props.state.trainResult.logs.some((l: TrainLog) => l.line === line && l.level === 'error')) {
          props.state.trainResult.logs.push({ step: data.step || '', level: 'error', line })
        }
      }
    }, props.state.featureReport.data?.csv_path, extractXgbNodeLabel(props.script))
    props.state.trainResult.data = result
    if (result) {
      // SSE result 包里 learning_curve 可能不完整, 用 SSE log 累积的实时点补齐
      if (!Array.isArray(result.learning_curve) || result.learning_curve.length === 0) {
        result.learning_curve = [...cachedLearningCurve.value]
      }
      emit('trained')
    }
  } finally {
    props.state.trainResult.loading = false
  }
}

async function onOptimize() {
  if (!props.state.featureReport.data) return
  props.state.optimizeRunning = true
  props.state.optimizeTrials = []
  props.state.optimizeResult = null
  props.state.optimizeProgress = '启动优化...'

  try {
    const result = await optimize(props.script, {
      labelSource: config.labelSource,
      labelPeriod: config.labelPeriod,
      labelType: config.labelType,
      labelShape: config.labelShape,
      volK: config.volK,
      objective: config.objective,
      numClass: config.numClass,
      testRatio: config.testRatio,
      valRatio: config.valRatio,
      startDate: props.state.dateRange.value?.[0] || '',
      endDate: props.state.dateRange.value?.[1] || '',
      frequency: props.state.frequency.value || '1d',
      csvPath: props.state.featureReport.data?.csv_path,
      optimizeMetric: props.state.optimizeMetric,
      nTrials: props.state.nTrials,
      paramDomains: props.state.paramDomains,
    }, (type: string, data: any) => {
      if (type === 'trial') {
        props.state.optimizeTrials.push(data)
        const okCount = props.state.optimizeTrials.filter((t: any) => t.status === 'ok').length
        const bestVal = data.best != null ? data.best.toFixed(4) : '—'
        props.state.optimizeProgress = `${props.state.optimizeTrials.length} / ${props.state.nTrials} trials  best: ${bestVal}`
      } else if (type === 'info') {
        if (data.phase === 'split') {
          props.state.optimizeProgress = `数据准备完成 (${data.n_train}/${data.n_val || 0}/${data.n_test})，开始搜索...`
        }
      } else if (type === 'log') {
        // 可选：记录日志
      }
    })

    if (result) {
      props.state.optimizeResult = result
      props.state.optimizeProgress = `优化完成 — 最佳 ${props.state.optimizeMetric}: ${result.best_value.toFixed(4)} (trial #${result.best_trial_number})`
    }
  } finally {
    props.state.optimizeRunning = false
  }
}

function resetDomains() {
  const defaults = props.state.DEFAULT_PARAM_DOMAINS
  for (const k of Object.keys(defaults)) {
    Object.assign(props.state.paramDomains[k], defaults[k])
  }
}
</script>

<style scoped>
.train-panel { padding: 16px 4px 24px; }

.train-summary {
  display: flex;
  justify-content: space-between;
  align-items: flex-start;
  gap: 16px;
  margin-bottom: 18px;
  padding: 16px 20px;
  background: linear-gradient(135deg, rgba(41, 98, 255, 0.18), rgba(41, 98, 255, 0.04) 70%, transparent);
  border: 1px solid rgba(74, 85, 104, 0.35);
  border-radius: 10px;
}
.summary-text h3 {
  margin: 4px 0 6px;
  font-size: 17px;
  color: #f1f5f9;
  font-weight: 600;
}
.summary-text p {
  margin: 0;
  color: #94a3b8;
  font-size: 12.5px;
  line-height: 1.6;
  max-width: 640px;
}
.summary-action {
  display: flex;
  gap: 8px;
  align-items: center;
  flex-shrink: 0;
}
.section-eyebrow {
  font-size: 10.5px;
  letter-spacing: 2.2px;
  color: #5b8ff9;
  font-weight: 600;
  text-transform: uppercase;
}

.btn {
  padding: 6px 16px;
  border-radius: 4px;
  font-size: 12px;
  cursor: pointer;
  border: none;
  transition: all 0.2s;
}
.btn-primary {
  background: #3b82f6;
  color: white;
}
.btn-primary:hover:not(:disabled) { background: #2563eb; }
.btn-primary:disabled { background: #475569; cursor: not-allowed; opacity: 0.7; }

.source-chip {
  display: inline-flex;
  align-items: center;
  gap: 4px;
  padding: 4px 10px;
  font-size: 11px;
  border-radius: 999px;
  background: rgba(91, 143, 249, 0.12);
  color: #93c5fd;
  font-family: 'SF Mono', 'Consolas', monospace;
  max-width: 200px;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}
.source-chip.warn {
  background: rgba(234, 57, 67, 0.12);
  color: #f87171;
}

.training-progress {
  margin-bottom: 16px;
  background: rgba(15, 25, 41, 0.55);
  border: 1px solid rgba(74, 85, 104, 0.25);
  border-radius: 8px;
  padding: 12px 16px;
}
.progress-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 10px;
}
.progress-title { font-size: 13px; font-weight: 600; color: #e2e8f0; }
.progress-done { font-size: 11px; color: #34d399; font-weight: 600; }
.step-list { display: flex; flex-direction: column; gap: 4px; }
.step-item {
  display: flex;
  align-items: center;
  gap: 8px;
  font-size: 12px;
  padding: 3px 0;
  flex-wrap: nowrap;
}
.step-icon {
  width: 18px;
  text-align: center;
  font-size: 12px;
  flex-shrink: 0;
}
.step-icon.done { color: #34d399; }
.step-icon.running { color: #fbbf24; }
.step-icon.error { color: #f87171; }
.step-icon.pending { color: #64748b; }
.step-label { color: #cbd5e1; white-space: nowrap; flex-shrink: 0; }
.step-detail {
  color: #94a3b8;
  font-size: 11px;
  font-family: 'SF Mono', 'Consolas', monospace;
  white-space: nowrap;
}
.step-time {
  color: #64748b;
  font-size: 11px;
  font-family: 'SF Mono', 'Consolas', monospace;
  min-width: 50px;
  text-align: right;
  margin-left: auto;
  flex-shrink: 0;
}
.step-progress-inline {
  display: flex;
  align-items: center;
  gap: 8px;
  flex: 1;
  min-width: 0;
}
.step-progress-bar {
  flex: 1;
  height: 4px;
  background: rgba(74, 85, 104, 0.3);
  border-radius: 2px;
  overflow: hidden;
  min-width: 60px;
}
.step-progress-fill {
  height: 100%;
  background: #3b82f6;
  border-radius: 2px;
  transition: width 0.3s ease;
}
.step-progress-pct {
  color: #5b8ff9;
  font-size: 11px;
  font-family: 'SF Mono', 'Consolas', monospace;
  min-width: 36px;
  text-align: right;
  flex-shrink: 0;
}
.log-item {
  display: flex;
  align-items: flex-start;
  gap: 6px;
  font-size: 11px;
  padding: 2px 0 2px 26px;
  font-family: 'SF Mono', 'Consolas', monospace;
}
.log-item.warning { color: #fbbf24; }
.log-item.error { color: #f87171; }
.log-item.info { color: #94a3b8; }
.log-icon { flex-shrink: 0; }
.log-line { word-break: break-all; }

.config-section, .chart-section {
  margin-bottom: 18px;
  background: rgba(15, 25, 41, 0.55);
  border: 1px solid rgba(74, 85, 104, 0.25);
  border-radius: 8px;
  padding: 14px 16px 16px;
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
.config-grid {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(220px, 1fr));
  gap: 12px;
}
.config-item label {
  display: flex;
  align-items: center;
  gap: 4px;
  color: #94a3b8;
  font-size: 12px;
  margin-bottom: 4px;
}
.config-hint {
  display: block;
  color: #64748b;
  font-size: 11px;
  margin-top: 2px;
  font-family: 'SF Mono', 'Consolas', monospace;
}
.tip-icon {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  width: 14px;
  height: 14px;
  border-radius: 50%;
  background: #3b4a6b;
  color: #94a3b8;
  font-size: 10px;
  font-weight: 600;
  flex-shrink: 0;
}
.select-input {
  width: 100%;
  padding: 6px 10px;
  background: rgba(26, 34, 54, 0.8);
  border: 1px solid rgba(74, 85, 104, 0.3);
  border-radius: 4px;
  color: #e0e0e0;
  font-size: 12px;
  outline: none;
  font-family: inherit;
}
.select-input:focus {
  border-color: rgba(41, 98, 255, 0.5);
}
.select-input:disabled {
  opacity: 0.5;
  cursor: not-allowed;
}
.readonly-input {
  background: rgba(26, 34, 54, 0.4);
  color: #94a3b8;
}

.advanced-toggle {
  width: 100%;
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 8px 0;
  background: transparent;
  border: none;
  color: #cbd5e1;
  font-size: 13px;
  cursor: pointer;
  text-align: left;
}
.advanced-toggle:hover { color: #e2e8f0; }
.advanced-hint {
  margin-left: auto;
  font-size: 11px;
  color: #64748b;
  font-weight: normal;
}
.advanced-body {
  padding-top: 8px;
  border-top: 1px solid rgba(74, 85, 104, 0.15);
}

.result-summary {
  display: flex;
  gap: 18px;
  margin-bottom: 18px;
  padding: 14px 20px;
  background: rgba(15, 25, 41, 0.55);
  border: 1px solid rgba(74, 85, 104, 0.25);
  border-radius: 8px;
}
.result-stat {
  display: flex;
  flex-direction: column;
  gap: 2px;
}
.result-label {
  font-size: 11px;
  color: #94a3b8;
  text-transform: uppercase;
  letter-spacing: 1px;
}
.result-value {
  font-size: 16px;
  color: #e2e8f0;
  font-weight: 600;
  font-family: 'SF Mono', 'Consolas', monospace;
}

.empty-state {
  padding: 60px 20px;
  text-align: center;
  color: #64748b;
}
.empty-icon { font-size: 48px; margin-bottom: 12px; }

/* ── 参数优化 ── */
.optimize-section {
  margin-bottom: 20px;
  border: 1px solid #2a2a3e;
  border-radius: 10px;
  background: #12121e;
  overflow: hidden;
}
.optimize-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 14px 18px;
  cursor: pointer;
  user-select: none;
}
.optimize-header:hover { background: #1a1a2e; }
.collapse-arrow {
  font-size: 11px;
  color: #666;
  transition: transform 0.2s;
}
.collapse-arrow.open { transform: rotate(90deg); }

.optimize-body { padding: 0 18px 18px; }

.optimize-config {
  display: flex;
  gap: 16px;
  align-items: flex-end;
  flex-wrap: wrap;
  margin-bottom: 16px;
}
.optimize-actions {
  display: flex;
  gap: 8px;
  align-items: flex-end;
}

.btn-accent {
  background: linear-gradient(135deg, #f5a623, #e08d1a);
  color: #1a1a2e;
  border: none;
  border-radius: 6px;
  padding: 8px 16px;
  font-weight: 600;
  font-size: 13px;
  cursor: pointer;
  display: flex;
  align-items: center;
  gap: 6px;
}
.btn-accent:hover { filter: brightness(1.1); }
.btn-accent:disabled { opacity: 0.5; cursor: not-allowed; }

.btn-ghost {
  background: transparent;
  color: #888;
  border: 1px solid #333;
  border-radius: 6px;
  padding: 8px 14px;
  font-size: 12px;
  cursor: pointer;
}
.btn-ghost:hover { border-color: #555; color: #ccc; }
.btn-ghost:disabled { opacity: 0.4; cursor: not-allowed; }

/* 参数域表格 */
.domain-table {
  border: 1px solid #222;
  border-radius: 8px;
  overflow: hidden;
  margin-bottom: 16px;
}
.domain-row {
  display: flex;
  align-items: center;
  padding: 6px 12px;
  border-bottom: 1px solid #1a1a2e;
}
.domain-row:last-child { border-bottom: none; }
.domain-header {
  background: #1a1a2e;
  font-size: 11px;
  color: #888;
  text-transform: uppercase;
  letter-spacing: 0.5px;
}
.domain-col { padding: 2px 6px; }
.domain-enable { width: 40px; text-align: center; }
.domain-name { flex: 1; font-size: 13px; color: #ccc; }
.domain-min, .domain-max { width: 100px; }
.domain-step { width: 80px; }
.domain-log { width: 50px; text-align: center; }
.domain-input {
  width: 100%;
  background: #1a1a2e;
  border: 1px solid #333;
  border-radius: 4px;
  color: #e0e0e0;
  padding: 4px 8px;
  font-size: 12px;
  font-family: 'SF Mono', 'Consolas', monospace;
}
.domain-input:focus { border-color: #5b8def; outline: none; }
.domain-na { color: #555; font-size: 12px; }

/* 进度条 */
.optimize-progress {
  display: flex;
  align-items: center;
  gap: 12px;
  margin-bottom: 16px;
}
.progress-bar {
  flex: 1;
  height: 6px;
  background: #1a1a2e;
  border-radius: 3px;
  overflow: hidden;
}
.progress-fill {
  height: 100%;
  background: linear-gradient(90deg, #5b8def, #8b5cf6);
  border-radius: 3px;
  transition: width 0.3s;
}
.progress-text {
  font-size: 12px;
  color: #999;
  white-space: nowrap;
  font-family: 'SF Mono', 'Consolas', monospace;
}

/* 结果区域 */
.optimize-result {
  border-top: 1px solid #2a2a3e;
  padding-top: 16px;
}
.result-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  margin-bottom: 16px;
}
.result-summary {
  display: flex;
  align-items: baseline;
  gap: 10px;
}
.result-badge {
  font-size: 12px;
  color: #f5a623;
  font-weight: 600;
}
.result-value {
  font-size: 22px;
  font-weight: 700;
  color: #e2e8f0;
  font-family: 'SF Mono', 'Consolas', monospace;
}
.result-trial {
  font-size: 12px;
  color: #888;
}
.result-duration {
  font-size: 12px;
  color: #666;
}

.result-charts {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 16px;
  margin-bottom: 16px;
}
.chart-box {
  background: #1a1a2e;
  border-radius: 8px;
  padding: 12px;
}
.chart-box h4 {
  font-size: 12px;
  color: #888;
  margin: 0 0 8px 0;
  font-weight: 500;
}

/* Trial 对比表 */
.trials-detail {
  margin-top: 12px;
}
.trials-detail summary {
  font-size: 13px;
  color: #888;
  cursor: pointer;
  padding: 8px 0;
}
.trials-detail summary:hover { color: #ccc; }
.trials-table-wrap {
  max-height: 300px;
  overflow: auto;
  border: 1px solid #222;
  border-radius: 6px;
}
.trials-table {
  width: 100%;
  border-collapse: collapse;
  font-size: 12px;
}
.trials-table th {
  background: #1a1a2e;
  color: #888;
  font-weight: 500;
  padding: 6px 10px;
  text-align: left;
  position: sticky;
  top: 0;
  z-index: 1;
}
.trials-table td {
  padding: 5px 10px;
  border-bottom: 1px solid #1a1a2e;
  color: #ccc;
  font-family: 'SF Mono', 'Consolas', monospace;
}
.trials-table .best-row { background: rgba(245, 166, 35, 0.1); }
.trials-table .best-row td { color: #f5a623; }
.trials-table .failed-row td { color: #666; text-decoration: line-through; }
</style>
