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
        <div class="card-value">{{ (props.data.indexValue ?? 0).toFixed(2) }}</div>
        <div class="card-change" :class="(props.data.changePercent ?? 0) >= 0 ? 'up' : 'down'">
          <i :class="(props.data.changePercent ?? 0) >= 0 ? 'fas fa-caret-up' : 'fas fa-caret-down'"></i>
          {{ (props.data.changePercent ?? 0) >= 0 ? '+' : '' }}{{ (props.data.changePercent ?? 0).toFixed(2) }}%
        </div>
      </div>
      <!-- 波动率卡片 -->
      <div class="risk-card">
        <div class="card-icon"><i class="fas fa-wave-square"></i></div>
        <div class="card-label">波动率</div>
        <div class="card-value">{{ (props.data.volatility ?? 0).toFixed(2) }}</div>
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
import { computed, onMounted, onBeforeUnmount } from 'vue'
import { useQuoteStore } from '@/stores/quoteStore'
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

// 沪深300指数实时价格 — 从 quoteStore 订阅，用于 IV Index 计算
const quoteStore = useQuoteStore()
const hs300Quote = computed(() => quoteStore.getQuote('SH000300'))
/** 沪深300当前价格，供外部组件或后续IV计算使用 */
const hs300Price = computed(() => hs300Quote.value.lastPrice)

onMounted(() => {
  quoteStore.subscribe('SH000300')
})

onBeforeUnmount(() => {
  quoteStore.unsubscribe('SH000300')
})
</script>

<style scoped lang="scss">
.market-risk-overview {
  background: var(--panel-bg);
  border: 1px solid var(--border);
  border-radius: 10px;
  padding: 20px;
  box-shadow: 0 4px 12px rgba(0, 0, 0, 0.15);

  .header {
    display: flex;
    justify-content: space-between;
    align-items: center;
    margin-bottom: 16px;

    h3 {
      margin: 0;
      font-size: 18px;
      color: var(--text);
      font-weight: 600;

      i {
        margin-right: 8px;
        color: var(--primary);
      }
    }

    .market-select {
      width: 140px;
    }
  }

  .cards {
    display: grid;
    grid-template-columns: repeat(4, 1fr);
    gap: 16px;
  }
}

.risk-card {
  background: var(--darker-bg);
  border: 1px solid var(--border);
  border-radius: 10px;
  padding: 20px;
  text-align: center;
  transition: all 0.2s;

  &:hover {
    box-shadow: 0 4px 12px rgba(0, 0, 0, 0.2);
    border-color: var(--primary);
  }

  .card-icon {
    font-size: 24px;
    color: var(--primary);
    margin-bottom: 8px;
  }

  .card-label {
    font-size: 13px;
    color: var(--text-secondary);
    margin-bottom: 6px;
    text-transform: uppercase;
    letter-spacing: 0.5px;
  }

  .card-value {
    font-size: 28px;
    font-weight: 700;
    margin: 8px 0;
    color: var(--text);
  }

  .card-change {
    font-size: 15px;
    font-weight: 600;

    &.up {
      color: var(--secondary); // A股涨=绿
    }

    &.down {
      color: #ff5252; // A股跌=红
    }
  }

  .card-sub {
    font-size: 12px;
    color: var(--text-secondary);
    margin-top: 6px;
  }

  .up {
    color: var(--secondary);
  }

  .down {
    color: #ff5252;
  }

  .sentiment-bar {
    height: 6px;
    background: var(--border);
    border-radius: 3px;
    margin-top: 12px;
    overflow: hidden;

    .sentiment-fill {
      height: 100%;
      border-radius: 3px;
      background: linear-gradient(90deg, var(--secondary) 0%, var(--accent) 50%, #ff5252 100%);
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
