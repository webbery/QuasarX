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
      color: var(--secondary);
    }
  }
}

.stock-grid {
  display: grid;
  grid-template-columns: repeat(2, 1fr);
  gap: 16px;
}

.stock-item {
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

  .stock-label {
    font-size: 12px;
    color: var(--text-secondary);
    margin-bottom: 8px;
    text-transform: uppercase;
    letter-spacing: 0.5px;
  }

  .stock-value {
    font-size: 24px;
    font-weight: 700;
    color: var(--text);

    .unit {
      font-size: 12px;
      font-weight: normal;
      color: var(--text-secondary);
    }
  }

  .stock-desc {
    font-size: 12px;
    color: var(--text-secondary);
    margin-top: 6px;
  }

  .stock-bar {
    width: 80%;
    margin: 10px auto 0;
    height: 6px;
    background: var(--border);
    border-radius: 3px;
    overflow: hidden;

    .stock-bar-fill {
      height: 100%;
      border-radius: 3px;
      transition: width 0.3s;

      &.low { background: var(--secondary); }
      &.medium { background: var(--accent); }
      &.high { background: #ff5252; }
    }
  }
}

@media (max-width: 700px) {
  .stock-grid {
    grid-template-columns: 1fr;
  }
}
</style>
