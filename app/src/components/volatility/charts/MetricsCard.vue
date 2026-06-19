<template>
  <div class="metrics-card">
    <h3 class="card-title">波动率与风险指标</h3>
    <div class="metrics-table">
      <div class="metric-row">
        <span class="metric-label">年化波动率</span>
        <span class="metric-value" :class="volClass">{{ formatPercent(data?.annual_volatility) }}</span>
      </div>
      <div class="metric-row">
        <span class="metric-label">最大回撤</span>
        <span class="metric-value warning">{{ formatPercent(data?.max_drawdown) }}</span>
      </div>
      <div class="metric-row">
        <span class="metric-label">偏度</span>
        <span class="metric-value" :class="skewClass">{{ formatNumber(data?.skewness) }}</span>
      </div>
      <div class="metric-row">
        <span class="metric-label">峰度</span>
        <span class="metric-value">{{ formatNumber(data?.kurtosis) }}</span>
      </div>
      <div class="metric-row">
        <span class="metric-label">VaR (95%)</span>
        <span class="metric-value danger">{{ formatPercent(data?.var_95) }}</span>
      </div>
      <div class="metric-row">
        <span class="metric-label">CVaR (95%)</span>
        <span class="metric-value danger">{{ formatPercent(data?.cvar_95) }}</span>
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

function formatPercent(value: number | undefined): string {
  return ((value || 0) * 100).toFixed(2) + '%'
}

function formatNumber(value: number | undefined): string {
  return (value || 0).toFixed(2)
}

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
  margin: 0 0 16px 0;
  font-size: 14px;
  color: #e0e0e0;
  font-weight: 600;
}

.metrics-table {
  display: grid;
  grid-template-columns: repeat(3, 1fr);
  gap: 16px;
}

.metric-row {
  display: flex;
  flex-direction: column;
  gap: 6px;
  padding: 12px;
  background: rgba(26, 34, 54, 0.4);
  border-radius: 6px;
  border: 1px solid rgba(74, 85, 104, 0.2);
}

.metric-label {
  font-size: 12px;
  color: #999;
  font-weight: 500;
}

.metric-value {
  font-size: 22px;
  font-weight: 600;
  color: #e0e0e0;
  font-family: 'Courier New', monospace;
}

.metric-value.high { color: #ef232a; }
.metric-value.medium { color: #ff9800; }
.metric-value.low { color: #00c853; }
.metric-value.warning { color: #ff9800; }
.metric-value.negative { color: #2962ff; }
.metric-value.positive { color: #ff9800; }
.metric-value.danger { color: #ef232a; }

@media (max-width: 1200px) {
  .metrics-table {
    grid-template-columns: repeat(2, 1fr);
  }
}

@media (max-width: 768px) {
  .metrics-table {
    grid-template-columns: 1fr;
  }
}
</style>
