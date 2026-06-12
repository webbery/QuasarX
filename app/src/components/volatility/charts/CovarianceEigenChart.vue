<template>
  <div class="eigen-card">
    <h3 class="card-title">协方差矩阵质量诊断</h3>
    <div class="metrics-grid">
      <div class="metric-item">
        <span class="metric-label">条件数 (κ)</span>
        <span class="metric-value" :class="conditionClass">
          {{ data?.condition_number?.toFixed(2) || '-' }}
        </span>
        <span class="metric-hint">{{ conditionHint }}</span>
      </div>
      <div class="metric-item">
        <span class="metric-label">正定性</span>
        <span class="metric-value" :class="pdClass">
          {{ data?.is_positive_definite ? '✓ 正定' : '✗ 非正定' }}
        </span>
      </div>
    </div>
    <div v-if="data?.eigenvalues?.length" class="eigen-values">
      <span class="metric-label">特征值: </span>
      <span class="eigen-list">{{ eigenStr }}</span>
    </div>
  </div>
</template>

<script setup lang="ts">
import { computed } from 'vue'

const props = defineProps<{
  data: {
    condition_number: number
    is_positive_definite: boolean
    eigenvalues: number[]
  } | null
}>()

const conditionClass = computed(() => {
  const cond = props.data?.condition_number || 0
  if (cond > 1000) return 'danger'
  if (cond > 100) return 'warning'
  return 'good'
})

const conditionHint = computed(() => {
  const cond = props.data?.condition_number || 0
  if (cond > 1000) return 'κ > 1000: 近共线性'
  if (cond > 100) return '100 < κ < 1000: 需注意'
  return 'κ < 100: 良好'
})

const pdClass = computed(() => props.data?.is_positive_definite ? 'good' : 'danger')

const eigenStr = computed(() =>
  props.data?.eigenvalues?.map(v => v.toExponential(2)).join(', ') || ''
)
</script>

<style scoped>
.eigen-card {
  background: rgba(36, 46, 66, 0.6);
  border: 1px solid rgba(74, 85, 104, 0.3);
  border-radius: 8px;
  padding: 16px;
}

.card-title {
  margin: 0 0 12px 0;
  font-size: 14px;
  color: #e0e0e0;
  font-weight: 600;
}

.metrics-grid {
  display: grid;
  grid-template-columns: repeat(2, 1fr);
  gap: 12px;
  margin-bottom: 12px;
}

.metric-item {
  display: flex;
  flex-direction: column;
  gap: 4px;
}

.metric-label {
  font-size: 12px;
  color: #999;
}

.metric-value {
  font-size: 20px;
  font-weight: 600;
}

.metric-value.good { color: #00c853; }
.metric-value.warning { color: #ff9800; }
.metric-value.danger { color: #ef232a; }

.metric-hint {
  font-size: 11px;
  color: #666;
}

.eigen-values {
  display: flex;
  align-items: center;
  gap: 8px;
  font-size: 12px;
}

.eigen-list {
  color: #a0aec0;
  font-family: monospace;
}
</style>
