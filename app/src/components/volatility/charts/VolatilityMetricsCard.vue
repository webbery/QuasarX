<template>
  <div class="metrics-card">
    <h3 class="card-title">波动率指标</h3>
    <div class="metrics-grid">
      <div class="metric-item">
        <span class="metric-label">年化波动率</span>
        <span class="metric-value" :class="volClass">{{ (data?.annual_volatility || 0) * 100 }}%</span>
      </div>
      <div class="metric-item">
        <span class="metric-label">最大回撤</span>
        <span class="metric-value warning">{{ (data?.max_drawdown || 0) * 100 }}%</span>
      </div>
      <div class="metric-item">
        <span class="metric-label">偏度</span>
        <span class="metric-value" :class="skewClass">{{ data?.skewness?.toFixed(3) || '0' }}</span>
      </div>
      <div class="metric-item">
        <span class="metric-label">峰度</span>
        <span class="metric-value">{{ data?.kurtosis?.toFixed(3) || '0' }}</span>
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { computed } from 'vue'
import type { VolatilitySingleResult } from '../composables/useVolatilityState'

const props = defineProps<{
  data: VolatilitySingleResult | null
}>()

const volClass = computed(() => {
  const vol = (props.data?.annual_volatility || 0) * 100
  if (vol > 50) return 'high'
  if (vol > 25) return 'medium'
  return 'low'
})

const skewClass = computed(() => {
  const skew = props.data?.skewness || 0
  if (skew < -1) return 'negative'
  if (skew > 1) return 'positive'
  return ''
})
</script>

<style scoped>
.metrics-card {
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
  color: #e0e0e0;
}

.metric-value.high { color: #ef232a; }
.metric-value.medium { color: #ff9800; }
.metric-value.low { color: #00c853; }
.metric-value.warning { color: #ff9800; }
.metric-value.negative { color: #2962ff; }
.metric-value.positive { color: #ff9800; }
</style>
