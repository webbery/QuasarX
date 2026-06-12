<template>
  <div class="risk-card">
    <h3 class="card-title">风险指标</h3>
    <div class="metrics-grid">
      <div class="metric-item">
        <span class="metric-label">VaR (95%)</span>
        <span class="metric-value danger">{{ (data?.var_95 || 0) * 100 }}%</span>
      </div>
      <div class="metric-item">
        <span class="metric-label">CVaR (95%)</span>
        <span class="metric-value danger">{{ (data?.cvar_95 || 0) * 100 }}%</span>
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import type { VolatilitySingleResult } from '../composables/useVolatilityState'

defineProps<{
  data: VolatilitySingleResult | null
}>()
</script>

<style scoped>
.risk-card {
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
  color: #ef232a;
}

.metric-value.danger { color: #ef232a; }
</style>
