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
      <!-- 当前模型信息 -->
      <div class="model-bar">
        <div class="model-bar-left">
          <span class="section-eyebrow">CURRENT MODEL</span>
          <div class="model-pills">
            <span class="pill primary">#{{ result.model_id }}</span>
            <span class="pill">{{ result.model_type || 'xgboost' }}</span>
            <span class="pill">{{ result.features.length }} features</span>
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
import { useMLData } from '../composables/useMLData'
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
const { shap } = useMLData()

const result = computed(() => props.state.trainResult.data)
const objective = computed(() => props.state.config.objective)
const isClassification = computed(() => {
  return objective.value === 'binary:logistic' || objective.value === 'multi:softprob'
})
const isBinary = computed(() => objective.value === 'binary:logistic')

const currentMetric = ref<Metric>('gain')
const shapLoading = ref(false)

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
