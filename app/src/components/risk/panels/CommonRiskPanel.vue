<template>
  <div class="common-risk-panel">
    <h4><i class="fas fa-chart-bar"></i> 通用风险指标</h4>
    <div class="metrics-grid">
      <div class="metric-card">
        <div class="metric-label">VaR (95%)</div>
        <div class="metric-value">{{ metrics.var_95?.toFixed(2) ?? '--' }}%</div>
      </div>
      <div class="metric-card">
        <div class="metric-label">最大回撤</div>
        <div class="metric-value negative">{{ metrics.maxDrawdown?.toFixed(2) ?? '--' }}%</div>
      </div>
      <div class="metric-card">
        <div class="metric-label">夏普比率</div>
        <div class="metric-value">{{ metrics.sharpeRatio?.toFixed(2) ?? '--' }}</div>
      </div>
      <div class="metric-card">
        <div class="metric-label">胜率</div>
        <div class="metric-value">{{ metrics.winRate ?? '--' }}%</div>
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
defineProps<{
  metrics: {
    var_95?: number
    maxDrawdown?: number
    sharpeRatio?: number
    winRate?: number
  }
}>()
</script>

<style scoped lang="scss">
.common-risk-panel {
  background: var(--panel-bg);
  border: 1px solid var(--border);
  border-radius: 10px;
  padding: 20px;
  margin-bottom: 16px;
  box-shadow: 0 4px 12px rgba(0, 0, 0, 0.15);

  h4 {
    margin: 0 0 16px;
    font-size: 16px;
    color: var(--text);
    font-weight: 600;

    i {
      margin-right: 8px;
      color: var(--primary);
    }
  }
}

.metrics-grid {
  display: grid;
  grid-template-columns: repeat(4, 1fr);
  gap: 16px;
}

.metric-card {
  background: var(--darker-bg);
  border: 1px solid var(--border);
  border-radius: 8px;
  padding: 16px;
  text-align: center;
  transition: all 0.2s;

  &:hover {
    border-color: var(--primary);
    box-shadow: 0 2px 8px rgba(41, 98, 255, 0.15);
  }

  .metric-label {
    font-size: 12px;
    color: var(--text-secondary);
    margin-bottom: 8px;
    text-transform: uppercase;
    letter-spacing: 0.5px;
  }

  .metric-value {
    font-size: 24px;
    font-weight: 700;
    color: var(--text);

    &.negative {
      color: #ff5252;
    }
  }
}

@media (max-width: 700px) {
  .metrics-grid {
    grid-template-columns: repeat(2, 1fr);
  }
}
</style>
