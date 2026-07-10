<template>
  <div class="train-panel">
    <!-- 配置区 -->
    <div class="config-section">
      <h3 class="section-title">训练配置</h3>
      <div class="config-grid">
        <div class="config-item">
          <label>标签来源</label>
          <select v-model="config.labelSource" class="select-input">
            <option value="">请选择...</option>
            <option v-for="f in featureColumns" :key="f" :value="f">{{ f }}</option>
          </select>
        </div>
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
          <label>分类阈值</label>
          <input type="number" v-model.number="config.threshold" step="0.01" class="select-input"
                 :disabled="config.labelType !== 'classification'" />
        </div>
        <div class="config-item">
          <label>目标函数</label>
          <select v-model="config.objective" class="select-input">
            <option v-for="o in XGBOOST_OBJECTIVES" :key="o.value" :value="o.value">{{ o.label }}</option>
          </select>
        </div>
        <div class="config-item">
          <label>学习率</label>
          <input type="number" v-model.number="config.learningRate" step="0.01" min="0.001" max="1.0" class="select-input" />
        </div>
        <div class="config-item">
          <label>最大深度</label>
          <input type="number" v-model.number="config.maxDepth" min="1" max="20" class="select-input" />
        </div>
        <div class="config-item">
          <label>迭代次数</label>
          <input type="number" v-model.number="config.nEstimators" min="10" max="2000" class="select-input" />
        </div>
        <div class="config-item">
          <label>早停轮数</label>
          <input type="number" v-model.number="config.earlyStoppingRounds" min="1" max="100" class="select-input" />
        </div>
        <div class="config-item">
          <label>测试集比例</label>
          <input type="number" v-model.number="config.testRatio" step="0.05" min="0.05" max="0.5" class="select-input" />
        </div>
      </div>
      <div class="action-bar">
        <button class="btn btn-primary" :disabled="state.trainResult.loading || !config.labelSource" @click="onTrain">
          {{ state.trainResult.loading ? '训练中…' : '开始训练' }}
        </button>
        <span v-if="state.trainResult.data" class="model-info">
          模型 ID: {{ state.trainResult.data.model_id }} | 训练 {{ state.trainResult.data.n_train }} 条，测试 {{ state.trainResult.data.n_test }} 条
        </span>
      </div>
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
import { XGBOOST_OBJECTIVES, LABEL_TYPES } from '../composables/useXGBoostState'
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

const { train, listFeatureColumns } = useXGBoostData()

const config = props.state.config
const featureColumns = ref<string[]>([])

watch(() => props.script, async (s) => {
  if (s) {
    featureColumns.value = await listFeatureColumns(s)
  }
}, { immediate: true })

function onLabelTypeChange() {
  if (config.labelType === 'regression') {
    config.objective = 'reg:squarederror'
  } else if (config.numClass === 2) {
    config.objective = 'binary:logistic'
  } else {
    config.objective = 'multi:softprob'
  }
}

async function onTrain() {
  props.state.trainResult.loading = true
  props.state.trainResult.progressMsg = '开始训练...'
  try {
    const result = await train(props.script, {
      labelSource: config.labelSource,
      labelPeriod: config.labelPeriod,
      labelType: config.labelType,
      threshold: config.threshold,
      objective: config.objective,
      numClass: config.numClass,
      testRatio: config.testRatio,
      learningRate: config.learningRate,
      maxDepth: config.maxDepth,
      nEstimators: config.nEstimators,
      earlyStoppingRounds: config.earlyStoppingRounds,
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
  display: block;
  color: #94a3b8;
  font-size: 12px;
  margin-bottom: 4px;
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
