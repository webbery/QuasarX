<template>
  <div class="future-risk-panel">
    <h4><i class="fas fa-exchange-alt"></i> 期货特有风险</h4>
    <div class="future-grid">
      <div class="future-item">
        <div class="future-label">保证金比例</div>
        <div class="future-value">{{ (data.marginRatio * 100).toFixed(1) }}%</div>
        <div class="future-desc">交易保证金占比</div>
      </div>
      <div class="future-item">
        <div class="future-label">杠杆暴露</div>
        <div class="future-value" :class="getLeverageClass(data.leverageExposure)">
          {{ data.leverageExposure.toFixed(1) }}x
        </div>
        <div class="future-desc">实际杠杆倍数</div>
      </div>
      <div class="future-item">
        <div class="future-label">期限结构</div>
        <div class="future-value">
          <span class="tag" :class="contangoTagClass(data.contangoBackwardation)">
            {{ contangoLabel(data.contangoBackwardation) }}
          </span>
        </div>
        <div class="future-desc">期货期限结构</div>
      </div>
      <div class="future-item">
        <div class="future-label">持仓量变化</div>
        <div class="future-value" :class="data.openInterestChange >= 0 ? 'positive' : 'negative'">
          {{ data.openInterestChange >= 0 ? '+' : '' }}{{ data.openInterestChange.toFixed(1) }}%
        </div>
        <div class="future-desc">较上期变动</div>
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import type { FutureRiskData } from '../types/risk'

defineProps<{
  data: FutureRiskData
}>()

function getLeverageClass(leverage: number): string {
  if (leverage > 5) return 'high-leverage'
  if (leverage > 3) return 'medium-leverage'
  return 'low-leverage'
}

function contangoTagClass(type: string): string {
  const map: Record<string, string> = {
    contango: 'tag-warning',
    backwardation: 'tag-danger',
    flat: 'tag-success',
  }
  return map[type] || 'tag-info'
}

function contangoLabel(type: string): string {
  const map: Record<string, string> = {
    contango: '升水',
    backwardation: '贴水',
    flat: '平水',
  }
  return map[type] || type
}
</script>

<style scoped lang="scss">
.future-risk-panel {
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

.future-grid {
  display: grid;
  grid-template-columns: repeat(4, 1fr);
  gap: 16px;
}

.future-item {
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

  .future-label {
    font-size: 12px;
    color: var(--text-secondary);
    margin-bottom: 8px;
    text-transform: uppercase;
    letter-spacing: 0.5px;
  }

  .future-value {
    font-size: 24px;
    font-weight: 700;
    color: var(--text);

    &.high-leverage {
      color: #ff5252;
    }

    &.medium-leverage {
      color: var(--accent);
    }

    &.low-leverage {
      color: var(--secondary);
    }

    &.positive {
      color: var(--secondary);
    }

    &.negative {
      color: #ff5252;
    }
  }

  .future-desc {
    font-size: 12px;
    color: var(--text-secondary);
    margin-top: 6px;
  }
}

// 标签样式
.tag {
  display: inline-block;
  padding: 4px 10px;
  font-size: 12px;
  border-radius: 4px;
  font-weight: 500;

  &.tag-success {
    background: rgba(0, 200, 83, 0.15);
    color: var(--secondary);
    border: 1px solid var(--secondary);
  }

  &.tag-warning {
    background: rgba(255, 109, 0, 0.15);
    color: var(--accent);
    border: 1px solid var(--accent);
  }

  &.tag-danger {
    background: rgba(255, 82, 82, 0.15);
    color: #ff5252;
    border: 1px solid #ff5252;
  }

  &.tag-info {
    background: rgba(41, 98, 255, 0.15);
    color: var(--primary);
    border: 1px solid var(--primary);
  }
}

@media (max-width: 700px) {
  .future-grid {
    grid-template-columns: repeat(2, 1fr);
  }
}
</style>
