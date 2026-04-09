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
          <el-tag :type="contangoTagType(data.contangoBackwardation)" size="small">
            {{ contangoLabel(data.contangoBackwardation) }}
          </el-tag>
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

function contangoTagType(type: string): 'danger' | 'warning' | 'success' {
  const map: Record<string, 'danger' | 'warning' | 'success'> = {
    contango: 'warning',
    backwardation: 'danger',
    flat: 'success',
  }
  return map[type] || 'info'
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
      color: var(--el-color-info);
    }
  }
}

.future-grid {
  display: grid;
  grid-template-columns: repeat(4, 1fr);
  gap: 12px;
}

.future-item {
  background: var(--el-fill-color-lighter);
  border: 1px solid var(--el-border-color-lighter);
  border-radius: 6px;
  padding: 12px;
  text-align: center;

  .future-label {
    font-size: 12px;
    color: var(--el-text-color-secondary);
    margin-bottom: 6px;
  }

  .future-value {
    font-size: 20px;
    font-weight: bold;
    color: var(--el-text-color-primary);

    &.high-leverage {
      color: #f56c6c;
    }

    &.medium-leverage {
      color: #e6a23c;
    }

    &.low-leverage {
      color: #67c23a;
    }

    &.positive {
      color: #f56c6c;
    }

    &.negative {
      color: #67c23a;
    }
  }

  .future-desc {
    font-size: 11px;
    color: var(--el-text-color-placeholder);
    margin-top: 4px;
  }
}

@media (max-width: 700px) {
  .future-grid {
    grid-template-columns: repeat(2, 1fr);
  }
}
</style>
