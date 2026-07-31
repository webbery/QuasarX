<template>
  <div class="metrics-card">
    <div class="metrics-grid">
      <div v-for="(value, key) in metrics" :key="key" class="metric-item">
        <div class="metric-label">{{ formatLabel(key) }}</div>
        <div class="metric-value">{{ formatValue(key, value) }}</div>
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
defineProps<{ metrics: Record<string, number> }>()

const LABELS: Record<string, string> = {
  accuracy: 'Accuracy',
  precision: 'Precision',
  recall: 'Recall',
  f1: 'F1 Score',
  auc: 'AUC',
  rmse: 'RMSE',
  mae: 'MAE',
  r2: 'R²',
  mape: 'MAPE',
}

function formatLabel(key: string): string {
  return LABELS[key] || key.toUpperCase()
}

function formatValue(key: string, value: number): string {
  if (typeof value !== 'number') return String(value)
  if (key === 'auc' || key === 'accuracy' || key === 'precision' || key === 'recall' || key === 'f1' || key === 'r2') {
    return value.toFixed(4)
  }
  if (key === 'rmse' || key === 'mae') return value.toFixed(4)
  if (key === 'mape') return value.toFixed(2) + '%'
  return value.toFixed(4)
}
</script>

<style scoped>
.metrics-card { padding: 12px; }
.metrics-grid {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(120px, 1fr));
  gap: 12px;
}
.metric-item {
  background: linear-gradient(180deg, #2b3a55, #1f2c44);
  border-radius: 8px;
  padding: 14px 16px;
  text-align: center;
  box-shadow: 0 1px 3px rgba(0,0,0,0.3);
}
.metric-label {
  color: #94a3b8;
  font-size: 12px;
  margin-bottom: 6px;
}
.metric-value {
  color: #e0e6f0;
  font-size: 20px;
  font-weight: 600;
}
</style>
