<template>
  <div class="market-risk-overview">
    <div class="header">
      <h3><i class="fas fa-globe"></i> 市场风险概览</h3>
      <el-select v-model="model" placeholder="选择市场" class="market-select" size="default">
        <el-option label="A股" value="astock" />
        <el-option label="期货" value="future" />
        <el-option label="期权" value="option" />
        <el-option label="美股" value="usstock" />
      </el-select>
    </div>
    <div class="cards">
      <!-- 市场指数卡片 -->
      <div class="risk-card">
        <div class="card-icon"><i class="fas fa-chart-line"></i></div>
        <div class="card-label">{{ props.data.indexName }}</div>
        <div class="card-value">{{ props.data.indexValue.toFixed(2) }}</div>
        <div class="card-change" :class="props.data.changePercent >= 0 ? 'up' : 'down'">
          <i :class="props.data.changePercent >= 0 ? 'fas fa-caret-up' : 'fas fa-caret-down'"></i>
          {{ props.data.changePercent >= 0 ? '+' : '' }}{{ props.data.changePercent.toFixed(2) }}%
        </div>
      </div>
      <!-- 波动率卡片 -->
      <div class="risk-card">
        <div class="card-icon"><i class="fas fa-wave-square"></i></div>
        <div class="card-label">波动率</div>
        <div class="card-value">{{ props.data.volatility.toFixed(2) }}</div>
        <div class="card-sub">IV Index</div>
      </div>
      <!-- 涨跌统计卡片 -->
      <div class="risk-card">
        <div class="card-icon"><i class="fas fa-balance-scale"></i></div>
        <div class="card-label">涨跌统计</div>
        <div class="card-value">
          <span class="up">{{ props.data.advanceCount > 0 ? props.data.advanceCount : '--' }}</span>
          /
          <span class="down">{{ props.data.declineCount > 0 ? props.data.declineCount : '--' }}</span>
        </div>
        <div class="card-sub">上涨 / 下跌</div>
      </div>
      <!-- 市场情绪卡片 -->
      <div class="risk-card">
        <div class="card-icon"><i class="fas fa-brain"></i></div>
        <div class="card-label">市场情绪</div>
        <div class="card-value">{{ props.data.sentimentIndex }}</div>
        <div class="card-sub">{{ props.data.sentimentLabel }}</div>
        <div class="sentiment-bar">
          <div class="sentiment-fill" :style="{ width: props.data.sentimentIndex + '%' }"></div>
        </div>
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { computed } from 'vue'
import type { MarketType } from './types/risk'

const props = defineProps<{
  marketType: MarketType
  data: {
    indexName: string
    indexValue: number
    changePercent: number
    volatility: number
    advanceCount: number
    declineCount: number
    sentimentIndex: number
    sentimentLabel: string
  }
}>()

const emit = defineEmits<{
  'update:marketType': [value: MarketType]
}>()

const model = computed({
  get: () => props.marketType,
  set: (v: MarketType) => emit('update:marketType', v),
})
</script>

<style scoped lang="scss">
.market-risk-overview {
  background: var(--el-bg-color);
  border: 1px solid var(--el-border-color-light);
  border-radius: 8px;
  padding: 16px;

  .header {
    display: flex;
    justify-content: space-between;
    align-items: center;
    margin-bottom: 12px;

    h3 {
      margin: 0;
      font-size: 16px;
      color: var(--el-text-color-primary);

      i {
        margin-right: 6px;
        color: var(--el-color-primary);
      }
    }

    .market-select {
      width: 140px;
    }
  }

  .cards {
    display: grid;
    grid-template-columns: repeat(4, 1fr);
    gap: 12px;
  }
}

.risk-card {
  background: var(--el-fill-color-lighter);
  border: 1px solid var(--el-border-color-lighter);
  border-radius: 8px;
  padding: 16px;
  text-align: center;
  transition: box-shadow 0.2s;

  &:hover {
    box-shadow: 0 2px 8px rgba(0, 0, 0, 0.08);
  }

  .card-icon {
    font-size: 20px;
    color: var(--el-color-primary);
    margin-bottom: 6px;
  }

  .card-label {
    font-size: 12px;
    color: var(--el-text-color-secondary);
    margin-bottom: 4px;
  }

  .card-value {
    font-size: 24px;
    font-weight: bold;
    margin: 6px 0;
    color: var(--el-text-color-primary);
  }

  .card-change {
    font-size: 14px;
    font-weight: 500;

    &.up {
      color: #f56c6c; // A股涨=红
    }

    &.down {
      color: #67c23a; // A股跌=绿
    }
  }

  .card-sub {
    font-size: 11px;
    color: var(--el-text-color-placeholder);
    margin-top: 4px;
  }

  .up {
    color: #f56c6c;
  }

  .down {
    color: #67c23a;
  }

  .sentiment-bar {
    height: 6px;
    background: var(--el-fill-color);
    border-radius: 3px;
    margin-top: 10px;
    overflow: hidden;

    .sentiment-fill {
      height: 100%;
      border-radius: 3px;
      background: linear-gradient(90deg, #67c23a 0%, #e6a23c 50%, #f56c6c 100%);
      transition: width 0.5s ease;
    }
  }
}

@media (max-width: 900px) {
  .market-risk-overview .cards {
    grid-template-columns: repeat(2, 1fr);
  }
}
</style>
