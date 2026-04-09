<template>
  <div class="option-risk-panel">
    <h4><i class="fas fa-coins"></i> 期权特有风险</h4>

    <div class="option-grid">
      <!-- Greeks 暴露 -->
      <div class="option-section">
        <h5>Greeks 暴露</h5>
        <div class="greeks-grid">
          <div class="greek-item">
            <div class="greek-label">Delta</div>
            <div class="greek-value" :class="data.greeks.delta >= 0 ? 'positive' : 'negative'">
              {{ data.greeks.delta.toFixed(2) }}
            </div>
            <div class="greek-desc">方向性暴露</div>
          </div>
          <div class="greek-item">
            <div class="greek-label">Gamma</div>
            <div class="greek-value">{{ data.greeks.gamma.toFixed(2) }}</div>
            <div class="greek-desc">Delta 敏感度</div>
          </div>
          <div class="greek-item">
            <div class="greek-label">Vega</div>
            <div class="greek-value" :class="data.greeks.vega >= 0 ? 'positive' : 'negative'">
              {{ data.greeks.vega.toFixed(2) }}
            </div>
            <div class="greek-desc">波动率暴露</div>
          </div>
          <div class="greek-item">
            <div class="greek-label">Theta</div>
            <div class="greek-value">{{ data.greeks.theta.toFixed(2) }}</div>
            <div class="greek-desc">时间衰减</div>
          </div>
        </div>
      </div>

      <!-- 流动性结构风险 -->
      <div class="option-section">
        <h5>流动性结构风险</h5>
        <div class="liquidity-grid">
          <div class="liquidity-item">
            <div class="liquidity-label">买卖价差</div>
            <div class="liquidity-value">{{ data.liquidity.bidAskSpread.toFixed(3) }}</div>
            <div class="liquidity-bar">
              <div
                class="liquidity-fill"
                :class="getSpreadRiskClass(data.liquidity.bidAskSpread)"
                :style="{ width: getSpreadPercent(data.liquidity.bidAskSpread) + '%' }"
              ></div>
            </div>
          </div>
          <div class="liquidity-item">
            <div class="liquidity-label">订单簿深度</div>
            <div class="liquidity-value">
              <el-tag :type="depthTagType(data.liquidity.orderBookDepth)" size="small">
                {{ depthLabel(data.liquidity.orderBookDepth) }}
              </el-tag>
            </div>
          </div>
          <div class="liquidity-item">
            <div class="liquidity-label">成交量分布</div>
            <div class="liquidity-value">{{ data.liquidity.volumeConcentration }}</div>
          </div>
          <div class="liquidity-item">
            <div class="liquidity-label">隐含波动率</div>
            <div class="liquidity-value">{{ data.liquidity.impliedVolatility.toFixed(1) }}%</div>
          </div>
        </div>
      </div>

      <!-- 波动率对比 -->
      <div class="option-section">
        <h5>波动率对比</h5>
        <div class="vol-comparison">
          <div class="vol-bar-container">
            <div class="vol-bar-label">IV</div>
            <div class="vol-bar">
              <div
                class="vol-bar-fill vol-iv"
                :style="{ width: Math.min(data.liquidity.impliedVolatility, 100) + '%' }"
              ></div>
            </div>
            <div class="vol-bar-value">{{ data.liquidity.impliedVolatility.toFixed(1) }}%</div>
          </div>
          <div class="vol-bar-container">
            <div class="vol-bar-label">HV(20)</div>
            <div class="vol-bar">
              <div
                class="vol-bar-fill vol-hv"
                :style="{ width: Math.min(data.liquidity.impliedVolatility * 0.85, 100) + '%' }"
              ></div>
            </div>
            <div class="vol-bar-value">{{ (data.liquidity.impliedVolatility * 0.85).toFixed(1) }}%</div>
          </div>
        </div>
      </div>

      <!-- 到期日风险 -->
      <div class="option-section">
        <h5>到期日风险</h5>
        <div class="expiry-grid">
          <div class="expiry-item">
            <div class="expiry-label">距到期</div>
            <div class="expiry-value">
              {{ data.expiryRisk.daysToExpiry }}
              <span class="expiry-unit">天</span>
            </div>
          </div>
          <div class="expiry-item">
            <div class="expiry-label">时间衰减加速</div>
            <div class="expiry-value">
              <el-tag
                :type="data.expiryRisk.timeDecayAccelerating ? 'danger' : 'success'"
                size="small"
              >
                {{ data.expiryRisk.timeDecayAccelerating ? '是 ⚠️' : '否' }}
              </el-tag>
            </div>
          </div>
        </div>
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import type { OptionRiskData } from '../types/risk'

defineProps<{
  data: OptionRiskData
}>()

function getSpreadRiskClass(spread: number): string {
  if (spread > 0.03) return 'high-risk'
  if (spread > 0.015) return 'medium-risk'
  return 'low-risk'
}

function getSpreadPercent(spread: number): number {
  return Math.min((spread / 0.05) * 100, 100)
}

function depthTagType(depth: string): 'danger' | 'warning' | 'success' {
  const map: Record<string, 'danger' | 'warning' | 'success'> = {
    low: 'danger',
    medium: 'warning',
    high: 'success',
  }
  return map[depth] || 'info'
}

function depthLabel(depth: string): string {
  const map: Record<string, string> = {
    low: '浅',
    medium: '中等',
    high: '深',
  }
  return map[depth] || depth
}
</script>

<style scoped lang="scss">
.option-risk-panel {
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
      color: var(--el-color-warning);
    }
  }

  h5 {
    margin: 0 0 10px;
    font-size: 14px;
    color: var(--el-text-color-regular);
  }
}

.option-grid {
  display: grid;
  grid-template-columns: repeat(2, 1fr);
  gap: 16px;
}

.option-section {
  background: var(--el-fill-color-lighter);
  border: 1px solid var(--el-border-color-lighter);
  border-radius: 6px;
  padding: 12px;
}

.greeks-grid {
  display: grid;
  grid-template-columns: repeat(2, 1fr);
  gap: 8px;
}

.greek-item {
  text-align: center;
  padding: 8px;
  background: var(--el-bg-color);
  border-radius: 4px;

  .greek-label {
    font-size: 11px;
    color: var(--el-text-color-secondary);
    margin-bottom: 4px;
  }

  .greek-value {
    font-size: 18px;
    font-weight: bold;

    &.positive {
      color: #f56c6c;
    }

    &.negative {
      color: #67c23a;
    }
  }

  .greek-desc {
    font-size: 11px;
    color: var(--el-text-color-placeholder);
    margin-top: 2px;
  }
}

.liquidity-grid {
  display: flex;
  flex-direction: column;
  gap: 8px;
}

.liquidity-item {
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 4px 0;

  .liquidity-label {
    width: 80px;
    font-size: 12px;
    color: var(--el-text-color-secondary);
    flex-shrink: 0;
  }

  .liquidity-value {
    font-size: 14px;
    font-weight: 500;
    min-width: 50px;
  }

  .liquidity-bar {
    flex: 1;
    height: 6px;
    background: var(--el-fill-color);
    border-radius: 3px;
    overflow: hidden;

    .liquidity-fill {
      height: 100%;
      border-radius: 3px;
      transition: width 0.3s;

      &.low-risk { background: #67c23a; }
      &.medium-risk { background: #e6a23c; }
      &.high-risk { background: #f56c6c; }
    }
  }
}

.vol-comparison {
  display: flex;
  flex-direction: column;
  gap: 8px;
}

.vol-bar-container {
  display: flex;
  align-items: center;
  gap: 8px;

  .vol-bar-label {
    width: 40px;
    font-size: 12px;
    color: var(--el-text-color-secondary);
  }

  .vol-bar {
    flex: 1;
    height: 12px;
    background: var(--el-fill-color);
    border-radius: 3px;
    overflow: hidden;

    .vol-bar-fill {
      height: 100%;
      border-radius: 3px;
      transition: width 0.3s;

      &.vol-iv { background: #e6a23c; }
      &.vol-hv { background: #909399; }
    }
  }

  .vol-bar-value {
    width: 55px;
    font-size: 12px;
    text-align: right;
    font-weight: 500;
  }
}

.expiry-grid {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 8px;
}

.expiry-item {
  text-align: center;
  padding: 8px;
  background: var(--el-bg-color);
  border-radius: 4px;

  .expiry-label {
    font-size: 11px;
    color: var(--el-text-color-secondary);
    margin-bottom: 4px;
  }

  .expiry-value {
    font-size: 18px;
    font-weight: bold;

    .expiry-unit {
      font-size: 12px;
      font-weight: normal;
      color: var(--el-text-color-placeholder);
    }
  }
}

@media (max-width: 700px) {
  .option-grid {
    grid-template-columns: 1fr;
  }
}
</style>
