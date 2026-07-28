<template>
  <div class="train-panel">
    <!-- 摘要 + 训练入口 -->
    <div class="train-summary">
      <div class="summary-text">
        <span class="section-eyebrow">TRAINING INPUT</span>
        <h3>调整基础训练参数</h3>
        <p>基础参数覆盖 80% 的调参场景；高级正则项在折叠区内，通常只在过拟合/欠拟合时调整。</p>
      </div>
      <div class="summary-action">
        <button class="btn btn-primary" :disabled="state.trainResult.loading || !config.labelSource" @click="onTrain">
          <i v-if="state.trainResult.loading" class="fas fa-spinner fa-spin"></i>
          {{ state.trainResult.loading ? '训练中…' : '开始训练' }}
        </button>
        <span v-if="config.labelSource" class="source-chip" :title="`标签来源：${config.labelSource}`">
          <i class="fas fa-tag"></i>{{ config.labelSource }}
        </span>
        <span v-else class="source-chip warn">
          <i class="fas fa-exclamation-circle"></i>未选择标签来源
        </span>
      </div>
    </div>

    <!-- 训练进度面板 -->
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
          <label>测试集比例</label>
          <input type="number" v-model.number="config.testRatio" step="0.05" min="0.05" max="0.5" class="select-input" />
        </div>
      </div>
    </div>

    <!-- 高级参数折叠 -->
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
        <span class="result-label">训练 / 测试</span>
        <span class="result-value">{{ state.trainResult.data.n_train }} / {{ state.trainResult.data.n_test }}</span>
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

    <!-- 评估指标卡片 -->
    <div v-if="state.trainResult.data?.eval_metrics" class="metrics-section">
      <div class="section-heading">
        <div>
          <span class="section-eyebrow">EVALUATION</span>
          <h3 class="section-title">评估指标</h3>
        </div>
      </div>
      <MetricsCard :metrics="state.trainResult.data.eval_metrics" />
    </div>

    <!-- 学习曲线 -->
    <div v-if="state.trainResult.data?.learning_curve?.length" class="chart-section">
      <div class="section-heading">
        <div>
          <span class="section-eyebrow">LEARNING CURVE</span>
          <h3 class="section-title">学习曲线</h3>
        </div>
        <span class="section-hint">训练/验证损失随迭代变化</span>
      </div>
      <LearningCurveChart :data="state.trainResult.data.learning_curve" />
    </div>

    <div v-if="!state.trainResult.data && !state.trainResult.loading" class="empty-state">
      <div class="empty-icon">📈</div>
      <div>确认标签来源并配置训练参数后点击"开始训练"</div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { computed, ref, watch } from 'vue'
import { LABEL_TYPES, REG_MODES } from '../composables/useXGBoostState'
import type { TrainStep, TrainLog } from '../composables/useXGBoostState'
import { useXGBoostData } from '../composables/useXGBoostData'
import LearningCurveChart from '../charts/LearningCurveChart.vue'
import MetricsCard from '../charts/MetricsCard.vue'

const showAdvanced = ref(false)

const props = defineProps<{
  state: any  // useXGBoostState() 的返回
  script: string  // 选中策略的 JSON 字符串
}>()

const emit = defineEmits<{
  (e: 'trained'): void
}>()

const { train } = useXGBoostData()

const config = props.state.config

// 正则化提示文本
const regHint = computed(() => {
  if (config.regMode === 'none') return '不施加正则化'
  if (config.regMode === 'l1') return `reg_alpha = ${config.regValue}`
  if (config.regMode === 'l2') return `reg_lambda = ${config.regValue}`
  return `reg_alpha = ${config.regValue}, reg_lambda = ${config.regValue}`
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

function onLabelTypeChange() {
  props.state.syncObjective()
}

const STEP_LABELS: Record<string, string> = {
  parse_script: '解析策略图',
  init_nodes: '初始化节点',
  start_exchange: '启动数据源',
  collect_data: '收集特征数据',
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
      volK: config.volK,
      objective: config.objective,
      numClass: config.numClass,
      testRatio: config.testRatio,
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
          // 重连重放：重置已有步骤而非新增
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
      // 更新步骤的进度信息（epoch 进度条）
      const step = props.state.trainResult.steps.find((s: TrainStep) => s.id === data.step)
      if (step && data.total > 0) {
        step.detail = `${data.current} / ${data.total} epochs`
      }
    } else if (type === 'warning') {
      const line = data.msg || data.line || ''
      // 去重：避免重连时重复显示相同警告
      if (!props.state.trainResult.logs.some((l: TrainLog) => l.line === line && l.level === 'warning')) {
        props.state.trainResult.logs.push({ step: data.step || '', level: 'warning', line })
      }
    } else if (type === 'log') {
      props.state.trainResult.logs.push({ step: data.step || '', level: 'info', line: data.line || '' })
    } else if (type === 'error') {
      const step = props.state.trainResult.steps.find((s: TrainStep) => s.id === data.step)
      if (step) step.status = 'error'
      const line = data.msg || ''
      if (!props.state.trainResult.logs.some((l: TrainLog) => l.line === line && l.level === 'error')) {
        props.state.trainResult.logs.push({ step: data.step || '', level: 'error', line })
      }
    }
  })
    props.state.trainResult.data = result
    if (result) emit('trained')
  } finally {
    props.state.trainResult.loading = false
  }
}
</script>

<style scoped>
.train-panel { padding: 16px 4px 24px; }

/* 训练进度面板 */
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
.progress-title {
  font-size: 13px;
  font-weight: 600;
  color: #e2e8f0;
}
.progress-done {
  font-size: 11px;
  color: #34d399;
  font-weight: 600;
}
.step-list {
  display: flex;
  flex-direction: column;
  gap: 4px;
}
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
.step-label {
  color: #cbd5e1;
  white-space: nowrap;
  flex-shrink: 0;
}
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

.config-section, .metrics-section, .chart-section {
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
.section-hint {
  font-size: 11.5px;
  color: #94a3b8;
}
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
  font-weight: bold;
  cursor: help;
  position: relative;
}
.tip-icon:hover {
  background: #4b5a7b;
  color: #e0e6f0;
}
.select-input {
  width: 100%;
  background: #0f1929;
  color: #e0e6f0;
  border: 1px solid #2b3a55;
  border-radius: 4px;
  padding: 6px 10px;
  font-size: 13px;
}
.readonly-input {
  opacity: 0.6;
  cursor: not-allowed;
}
.train-summary {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 24px;
  margin-bottom: 16px;
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
  max-width: 540px;
}
.summary-action {
  display: flex;
  flex-direction: column;
  align-items: flex-end;
  gap: 8px;
}
.btn {
  background: #3b82f6;
  color: white;
  border: none;
  padding: 8px 20px;
  border-radius: 4px;
  cursor: pointer;
  font-size: 13px;
  display: inline-flex;
  align-items: center;
  gap: 6px;
}
.btn:disabled { background: #475569; cursor: not-allowed; }
.source-chip {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  padding: 3px 10px;
  background: rgba(91, 143, 249, 0.12);
  color: #5b8ff9;
  border: 1px solid rgba(91, 143, 249, 0.35);
  border-radius: 999px;
  font-size: 11.5px;
  font-family: 'SF Mono', 'Consolas', monospace;
  max-width: 280px;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}
.source-chip.warn {
  background: rgba(234, 179, 8, 0.12);
  color: #fbbf24;
  border-color: rgba(234, 179, 8, 0.35);
}
.advanced-toggle {
  width: 100%;
  display: flex;
  align-items: center;
  gap: 8px;
  background: transparent;
  border: none;
  color: #cbd5e1;
  font-size: 13px;
  font-weight: 600;
  cursor: pointer;
  padding: 4px 2px;
  text-align: left;
}
.advanced-toggle i { color: #5b8ff9; }
.advanced-hint {
  margin-left: auto;
  font-weight: 400;
  font-size: 11px;
  color: #94a3b8;
}
.advanced-body {
  margin-top: 12px;
  padding-top: 12px;
  border-top: 1px dashed rgba(74, 85, 104, 0.4);
}
.result-summary {
  display: grid;
  grid-template-columns: repeat(4, minmax(0, 1fr));
  gap: 12px;
  margin-bottom: 18px;
  padding: 12px 16px;
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
  font-size: 10.5px;
  color: #94a3b8;
  letter-spacing: 1.2px;
  text-transform: uppercase;
}
.result-value {
  font-size: 16px;
  color: #f1f5f9;
  font-weight: 600;
  font-family: 'SF Mono', 'Consolas', monospace;
}
.empty-state {
  padding: 60px 20px;
  text-align: center;
  color: #64748b;
}
.empty-icon { font-size: 48px; margin-bottom: 12px; }
</style>
