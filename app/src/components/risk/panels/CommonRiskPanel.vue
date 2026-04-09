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
  background: var(--el-bg-color);
  border: 1px solid var(--el-border-color-light);
  border-radius: 8px;
  padding: 16px;
  margin-bottom: 16px;

  h4 {
    margin: 0 0 12px;
    font-size: 15px;
    color: var(--el-text-color-primary);

    i {
      margin-right: 6px;
      color: var(--el-color-primary);
    }
  }
}

.metrics-grid {
  display: grid;
  grid-template-columns: repeat(4, 1fr);
  gap: 12px;
}

.metric-card {
  background: var(--el-fill-color-lighter);
  border: 1px solid var(--el-border-color-lighter);
  border-radius: 6px;
  padding: 12px;
  text-align: center;

  .metric-label {
    font-size: 12px;
    color: var(--el-text-color-secondary);
    margin-bottom: 6px;
  }

  .metric-value {
    font-size: 20px;
    font-weight: bold;
    color: var(--el-text-color-primary);

    &.negative {
      color: #f56c6c;
    }
  }
}

@media (max-width: 700px) {
  .metrics-grid {
    grid-template-columns: repeat(2, 1fr);
  }
}
</style>
