<template>
  <div class="stock-risk-panel">
    <h4><i class="fas fa-chart-pie"></i> 股票特有风险</h4>
    <div class="stock-grid">
      <div class="stock-item">
        <div class="stock-label">行业集中度</div>
        <div class="stock-value">
          <div class="value-text">{{ (data.sectorConcentration * 100).toFixed(1) }}%</div>
          <div class="stock-bar">
            <div
              class="stock-bar-fill"
              :class="getConcentrationClass(data.sectorConcentration)"
              :style="{ width: (data.sectorConcentration * 100) + '%' }"
            ></div>
          </div>
        </div>
      </div>
      <div class="stock-item">
        <div class="stock-label">Beta 加权</div>
        <div class="stock-value">{{ data.betaWeighted.toFixed(2) }}</div>
        <div class="stock-desc">市场敏感度</div>
      </div>
      <div class="stock-item">
        <div class="stock-label">换手率</div>
        <div class="stock-value">{{ (data.turnoverRate * 100).toFixed(1) }}%</div>
        <div class="stock-desc">月均换手</div>
      </div>
      <div class="stock-item">
        <div class="stock-label">平均持仓天数</div>
        <div class="stock-value">{{ data.avgHoldingDays }} <span class="unit">天</span></div>
        <div class="stock-desc">持仓周期</div>
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import type { StockRiskData } from '../types/risk'

defineProps<{
  data: StockRiskData
}>()

function getConcentrationClass(value: number): string {
  if (value > 0.7) return 'high'
  if (value > 0.4) return 'medium'
  return 'low'
}
</script>

<style scoped lang="scss">
.stock-risk-panel {
  background: var(--el-bg-color);
  border: 1px solid var(--el-border-color-light);
  border-radius: 8px;
  padding: 16px;
  margin-bottom: 16px;

  h4 {
    margin: 0 0 16px;
    font-size: 15px;

    i {
      margin-right: 6px;
      color: var(--el-color-success);
    }
  }
}

.stock-grid {
  display: grid;
  grid-template-columns: repeat(2, 1fr);
  gap: 12px;
}

.stock-item {
  background: var(--el-fill-color-lighter);
  border: 1px solid var(--el-border-color-lighter);
  border-radius: 6px;
  padding: 12px;
  text-align: center;

  .stock-label {
    font-size: 12px;
    color: var(--el-text-color-secondary);
    margin-bottom: 6px;
  }

  .stock-value {
    font-size: 20px;
    font-weight: bold;
    color: var(--el-text-color-primary);

    .unit {
      font-size: 12px;
      font-weight: normal;
      color: var(--el-text-color-placeholder);
    }
  }

  .stock-desc {
    font-size: 11px;
    color: var(--el-text-color-placeholder);
    margin-top: 4px;
  }

  .stock-bar {
    width: 80%;
    margin: 8px auto 0;
    height: 6px;
    background: var(--el-fill-color);
    border-radius: 3px;
    overflow: hidden;

    .stock-bar-fill {
      height: 100%;
      border-radius: 3px;
      transition: width 0.3s;

      &.low { background: #67c23a; }
      &.medium { background: #e6a23c; }
      &.high { background: #f56c6c; }
    }
  }
}

@media (max-width: 700px) {
  .stock-grid {
    grid-template-columns: 1fr;
  }
}
</style>
