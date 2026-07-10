<template>
  <div class="diagnostic-panel">
    <div v-if="!result" class="empty-state">
      <div class="empty-icon">🔬</div>
      <div>请先在"训练"面板完成训练</div>
    </div>
    <template v-else>
      <div v-if="isClassification" class="section">
        <h3 class="section-title">分类诊断</h3>
        <div class="chart-grid">
          <div class="chart-card">
            <RocCurveChart :predictions="result.predictions" />
          </div>
          <div class="chart-card">
            <ConfusionMatrixChart :predictions="result.predictions" />
          </div>
          <div class="chart-card full">
            <ProbabilityDistChart :predictions="result.predictions" />
          </div>
        </div>
      </div>
      <div v-else class="section">
        <h3 class="section-title">回归诊断</h3>
        <div class="chart-grid">
          <div class="chart-card full">
            <PredVsActualChart :predictions="result.predictions" />
          </div>
          <div class="chart-card full">
            <ResidualDistChart :predictions="result.predictions" />
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
const isClassification = computed(() => {
  const obj = props.state.config.objective
  return obj === 'binary:logistic' || obj === 'multi:softprob'
})
</script>

<style scoped>
.diagnostic-panel { padding: 16px; }
.section {
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
.chart-grid {
  display: grid;
  grid-template-columns: repeat(2, 1fr);
  gap: 16px;
}
.chart-card {
  background: #0f1929;
  border-radius: 6px;
  padding: 12px;
}
.chart-card.full { grid-column: span 2; }
.empty-state {
  padding: 60px 20px;
  text-align: center;
  color: #64748b;
}
.empty-icon { font-size: 48px; margin-bottom: 12px; }
</style>
