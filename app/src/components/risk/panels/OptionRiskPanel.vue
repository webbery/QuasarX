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
              <span class="tag" :class="depthTagClass(data.liquidity.orderBookDepth)">
                {{ depthLabel(data.liquidity.orderBookDepth) }}
              </span>
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
              <span
                class="tag"
                :class="data.expiryRisk.timeDecayAccelerating ? 'tag-danger' : 'tag-success'"
              >
                {{ data.expiryRisk.timeDecayAccelerating ? '是 ⚠️' : '否' }}
              </span>
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

function depthTagClass(depth: string): string {
  const map: Record<string, string> = {
    low: 'tag-danger',
    medium: 'tag-warning',
    high: 'tag-success',
  }
  return map[depth] || 'tag-info'
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
      color: var(--accent);
    }
  }

  h5 {
    margin: 0 0 12px;
    font-size: 14px;
    color: var(--text);
    font-weight: 600;
  }
}

.option-grid {
  display: grid;
  grid-template-columns: repeat(2, 1fr);
  gap: 20px;
}

.option-section {
  background: var(--darker-bg);
  border: 1px solid var(--border);
  border-radius: 8px;
  padding: 16px;
  transition: all 0.2s;

  &:hover {
    border-color: var(--primary);
  }
}

.greeks-grid {
  display: grid;
  grid-template-columns: repeat(2, 1fr);
  gap: 12px;
}

.greek-item {
  text-align: center;
  padding: 12px;
  background: var(--panel-bg);
  border: 1px solid var(--border);
  border-radius: 6px;
  transition: all 0.2s;

  &:hover {
    border-color: var(--primary);
  }

  .greek-label {
    font-size: 12px;
    color: var(--text-secondary);
    margin-bottom: 6px;
    text-transform: uppercase;
    letter-spacing: 0.5px;
  }

  .greek-value {
    font-size: 20px;
    font-weight: 700;
    color: var(--text);

    &.positive {
      color: var(--secondary);
    }

    &.negative {
      color: #ff5252;
    }
  }

  .greek-desc {
    font-size: 11px;
    color: var(--text-secondary);
    margin-top: 4px;
  }
}

.liquidity-grid {
  display: flex;
  flex-direction: column;
  gap: 10px;
}

.liquidity-item {
  display: flex;
  align-items: center;
  gap: 10px;
  padding: 6px 0;

  .liquidity-label {
    width: 90px;
    font-size: 12px;
    color: var(--text-secondary);
    flex-shrink: 0;
  }

  .liquidity-value {
    font-size: 14px;
    font-weight: 600;
    min-width: 50px;
    color: var(--text);
  }

  .liquidity-bar {
    flex: 1;
    height: 6px;
    background: var(--border);
    border-radius: 3px;
    overflow: hidden;

    .liquidity-fill {
      height: 100%;
      border-radius: 3px;
      transition: width 0.3s;

      &.low-risk { background: var(--secondary); }
      &.medium-risk { background: var(--accent); }
      &.high-risk { background: #ff5252; }
    }
  }
}

.vol-comparison {
  display: flex;
  flex-direction: column;
  gap: 10px;
}

.vol-bar-container {
  display: flex;
  align-items: center;
  gap: 10px;

  .vol-bar-label {
    width: 50px;
    font-size: 12px;
    color: var(--text-secondary);
    font-weight: 500;
  }

  .vol-bar {
    flex: 1;
    height: 12px;
    background: var(--border);
    border-radius: 3px;
    overflow: hidden;

    .vol-bar-fill {
      height: 100%;
      border-radius: 3px;
      transition: width 0.3s;

      &.vol-iv { background: var(--accent); }
      &.vol-hv { background: var(--text-secondary); }
    }
  }

  .vol-bar-value {
    width: 60px;
    font-size: 12px;
    text-align: right;
    font-weight: 600;
    color: var(--text);
  }
}

.expiry-grid {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 12px;
}

.expiry-item {
  text-align: center;
  padding: 12px;
  background: var(--panel-bg);
  border: 1px solid var(--border);
  border-radius: 6px;
  transition: all 0.2s;

  &:hover {
    border-color: var(--primary);
  }

  .expiry-label {
    font-size: 12px;
    color: var(--text-secondary);
    margin-bottom: 6px;
    text-transform: uppercase;
    letter-spacing: 0.5px;
  }

  .expiry-value {
    font-size: 20px;
    font-weight: 700;
    color: var(--text);

    .expiry-unit {
      font-size: 12px;
      font-weight: normal;
      color: var(--text-secondary);
    }
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
  .option-grid {
    grid-template-columns: 1fr;
  }
}
</style>
