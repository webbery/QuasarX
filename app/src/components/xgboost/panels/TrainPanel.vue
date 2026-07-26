<template>
  <div class="train-panel">
    <!-- 基础配置 -->
    <div class="config-section">
      <h3 class="section-title">标签配置</h3>
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

    <!-- 训练参数 -->
    <div class="config-section">
      <h3 class="section-title">训练参数</h3>
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

    <!-- 采样与正则化 -->
    <div class="config-section">
      <h3 class="section-title">采样与正则化</h3>
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

    <!-- 训练按钮 -->
    <div class="action-bar">
      <button class="btn btn-primary" :disabled="state.trainResult.loading || !config.labelSource" @click="onTrain">
        {{ state.trainResult.loading ? '训练中…' : '开始训练' }}
      </button>
      <span v-if="state.trainResult.data" class="model-info">
        模型 ID: {{ state.trainResult.data.model_id }} | 训练 {{ state.trainResult.data.n_train }} 条，测试 {{ state.trainResult.data.n_test }} 条
      </span>
    </div>

    <!-- 评估指标卡片 -->
    <div v-if="state.trainResult.data?.eval_metrics" class="metrics-section">
      <h3 class="section-title">评估指标</h3>
      <MetricsCard :metrics="state.trainResult.data.eval_metrics" />
    </div>

    <!-- 学习曲线 -->
    <div v-if="state.trainResult.data?.learning_curve?.length" class="chart-section">
      <h3 class="section-title">学习曲线</h3>
      <LearningCurveChart :data="state.trainResult.data.learning_curve" />
    </div>

    <div v-if="!state.trainResult.data && !state.trainResult.loading" class="empty-state">
      <div class="empty-icon">📈</div>
      <div>选择策略并配置训练参数后点击"开始训练"</div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { computed, ref, watch } from 'vue'
import { LABEL_TYPES, REG_MODES } from '../composables/useXGBoostState'
import { useXGBoostData } from '../composables/useXGBoostData'
import LearningCurveChart from '../charts/LearningCurveChart.vue'
import MetricsCard from '../charts/MetricsCard.vue'

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
            const codes = Array.isArray(codeParam.value) ? codeParam.value : [codeParam.value]
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

async function onTrain() {
  props.state.trainResult.loading = true
  props.state.trainResult.progressMsg = '开始训练...'
  try {
    // 根据正则化模式计算 reg_alpha / reg_lambda
    let regAlpha = 0, regLambda = 0
    if (config.regMode === 'l1') regAlpha = config.regValue
    else if (config.regMode === 'l2') regLambda = config.regValue
    else if (config.regMode === 'both') { regAlpha = config.regValue; regLambda = config.regValue }

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
      // 时间范围与标签分析一致
      startDate: props.state.dateRange?.[0] || '',
      endDate: props.state.dateRange?.[1] || '',
      frequency: props.state.frequency?.value || '1d',
    })
    props.state.trainResult.data = result
    if (result) emit('trained')
  } finally {
    props.state.trainResult.loading = false
  }
}
</script>

<style scoped>
.train-panel { padding: 16px; }
.config-section, .metrics-section, .chart-section {
  margin-bottom: 24px;
  background: #1e2a3a;
  border-radius: 8px;
  padding: 16px;
}
.section-title {
  color: #cbd5e1;
  font-size: 14px;
  margin: 0 0 12px;
  font-weight: 600;
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
.action-bar {
  margin-top: 16px;
  display: flex;
  align-items: center;
  gap: 16px;
}
.btn {
  background: #3b82f6;
  color: white;
  border: none;
  padding: 8px 20px;
  border-radius: 4px;
  cursor: pointer;
  font-size: 13px;
}
.btn:disabled { background: #555; cursor: not-allowed; }
.model-info { color: #94a3b8; font-size: 12px; }
.empty-state {
  padding: 60px 20px;
  text-align: center;
  color: #64748b;
}
.empty-icon { font-size: 48px; margin-bottom: 12px; }
</style>
