<template>
  <div class="diagnostic-panel">
    <div v-if="!result" class="stage-empty">
      <div class="stage-icon">04</div>
      <div>
        <span class="section-eyebrow">DIAGNOSTICS</span>
        <h3>训练完成后再进行模型诊断</h3>
        <p>分类模型会展示 ROC、混淆矩阵、概率分布；回归模型展示预测-实际、残差分布。</p>
      </div>
    </div>
    <template v-else>
      <div class="diagnostic-summary">
        <div class="summary-text">
          <span class="section-eyebrow">{{ isClassification ? 'CLASSIFICATION' : 'REGRESSION' }} REPORT</span>
          <h3>{{ isClassification ? (isBinary ? '二分类诊断' : '多分类诊断') : '回归诊断' }}</h3>
          <p>
            {{
              isBinary
                ? 'ROC 曲线衡量正负类分离度，混淆矩阵展示阈值=0.5 下的分类详情。'
                : isClassification
                  ? '默认使用 one-vs-rest 方式生成宏平均 ROC；混淆矩阵展示全部类别的分类结果。'
                  : '散点图越靠近对角线越好，残差分布应近似零中心对称。'
            }}
          </p>
        </div>
        <div v-if="isMultiClass" class="multi-class-warning">
          <i class="fas fa-info-circle"></i>
          预测样本中包含 0/1/2 三分类标签，已自动按 one-vs-rest 生成宏平均 ROC
        </div>
      </div>

      <div v-if="isClassification" class="section">
        <div class="section-heading">
          <div>
            <span class="section-eyebrow">DISCRIMINATION</span>
            <h3 class="section-title">分离度</h3>
          </div>
        </div>
        <div class="chart-grid">
          <div class="chart-card">
            <RocCurveChart :predictions="result.predictions" :objective="objective" />
          </div>
          <div class="chart-card">
            <ProbabilityDistChart
              v-if="isBinary"
              :predictions="result.predictions"
            />
            <div v-else class="chart-placeholder">
              <i class="fas fa-layer-group"></i>
              <span>多分类概率分布建议查看混淆矩阵</span>
            </div>
          </div>
        </div>
      </div>
      <div v-else class="section">
        <div class="section-heading">
          <div>
            <span class="section-eyebrow">REGRESSION QUALITY</span>
            <h3 class="section-title">拟合质量</h3>
          </div>
        </div>
        <div class="chart-grid">
          <div class="chart-card full">
            <PredVsActualChart :predictions="result.predictions" />
          </div>
          <div class="chart-card full">
            <ResidualDistChart :predictions="result.predictions" />
          </div>
        </div>
      </div>

      <div class="section">
        <div class="section-heading">
          <div>
            <span class="section-eyebrow">CONFUSION</span>
            <h3 class="section-title">混淆矩阵</h3>
          </div>
        </div>
        <div class="chart-grid">
          <div class="chart-card full">
            <ConfusionMatrixChart :predictions="result.predictions" :objective="objective" />
          </div>
        </div>
      </div>
    </template>
  </div>
</template>

<script setup lang="ts">
import { computed } from 'vue'
import RocCurveChart from '../charts/RocCurveChart.vue'
import ConfusionMatrixChart from '../charts/ConfusionMatrixChart.vue'
import ProbabilityDistChart from '../charts/ProbabilityDistChart.vue'
import PredVsActualChart from '../charts/PredVsActualChart.vue'
import ResidualDistChart from '../charts/ResidualDistChart.vue'

const props = defineProps<{ state: any }>()
const result = computed(() => props.state.trainResult.data)
const objective = computed(() => props.state.config.objective)
const isClassification = computed(() => {
  return objective.value === 'binary:logistic' || objective.value === 'multi:softprob'
})
const isBinary = computed(() => objective.value === 'binary:logistic')
const isMultiClass = computed(() => objective.value === 'multi:softprob')
</script>

<style scoped>
.diagnostic-panel { padding: 12px 4px 24px; }
.diagnostic-summary {
  display: flex;
  align-items: stretch;
  justify-content: space-between;
  gap: 18px;
  margin-bottom: 16px;
  padding: 16px 20px;
  background: linear-gradient(135deg, rgba(41, 98, 255, 0.18), rgba(41, 98, 255, 0.04) 70%, transparent);
  border: 1px solid rgba(74, 85, 104, 0.35);
  border-radius: 10px;
}
.diagnostic-summary h3 {
  margin: 4px 0 6px;
  font-size: 17px;
  color: #f1f5f9;
  font-weight: 600;
}
.diagnostic-summary p {
  margin: 0;
  color: #94a3b8;
  font-size: 12.5px;
  line-height: 1.6;
  max-width: 520px;
}
.multi-class-warning {
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 8px 12px;
  background: rgba(91, 143, 249, 0.12);
  border: 1px solid rgba(91, 143, 249, 0.35);
  color: #93c5fd;
  font-size: 12px;
  border-radius: 8px;
  max-width: 280px;
  align-self: center;
}
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
</style>
