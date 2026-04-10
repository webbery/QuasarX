<template>
  <div class="strategy-risk-list">
    <table class="risk-table">
      <thead>
        <tr>
          <th class="col-rank">排名</th>
          <th class="col-name">策略名称</th>
          <th class="col-type">类型</th>
          <th class="col-risk">风险等级</th>
          <th class="col-var">VaR(95%)</th>
          <th class="col-drawdown">最大回撤</th>
          <th class="col-sharpe">夏普比率</th>
          <th class="col-winrate">胜率</th>
          <th class="col-action">操作</th>
        </tr>
      </thead>
      <tbody>
        <tr
          v-for="(row, index) in sortedStrategies"
          :key="row.id"
          class="risk-row"
          :class="[`risk-row-${row.riskLevel}`, { 'is-stripe': index % 2 === 1 }]"
          @click="onRowClick(row)"
        >
          <!-- 排名 -->
          <td class="col-rank">
            <span class="rank-badge" :class="getRankClass(index)">
              {{ index + 1 }}
            </span>
          </td>

          <!-- 策略名称 -->
          <td class="col-name">
            <div class="strategy-name">{{ row.name }}</div>
            <div class="strategy-desc">{{ row.description }}</div>
          </td>

          <!-- 类型 -->
          <td class="col-type">
            <span class="type-tag">{{ strategyTypeLabel(row.strategyType) }}</span>
          </td>

          <!-- 风险等级 -->
          <td class="col-risk">
            <div class="risk-level-cell">
              <div class="risk-bars">
                <div
                  v-for="i in 5"
                  :key="i"
                  class="risk-bar"
                  :class="{ active: i <= riskBarCount(row.riskLevel) }"
                ></div>
              </div>
              <span :class="riskLevelClass(row.riskLevel)">
                {{ riskLevelLabel(row.riskLevel) }}
              </span>
            </div>
          </td>

          <!-- VaR(95%) -->
          <td class="col-var">
            <span>{{ row.var_95.toFixed(1) }}%</span>
          </td>

          <!-- 最大回撤 -->
          <td class="col-drawdown">
            <span class="negative">{{ row.maxDrawdown.toFixed(1) }}%</span>
          </td>

          <!-- 夏普比率 -->
          <td class="col-sharpe">
            <span>{{ row.sharpeRatio.toFixed(2) }}</span>
          </td>

          <!-- 胜率 -->
          <td class="col-winrate">
            <span>{{ row.winRate }}%</span>
          </td>

          <!-- 操作 -->
          <td class="col-action">
            <button class="config-btn" @click.stop="onConfigClick(row)" title="配置">
              <i class="fas fa-cog"></i>
            </button>
          </td>
        </tr>
      </tbody>
    </table>
  </div>
</template>

<script setup lang="ts">
import { computed } from 'vue'
import type { StrategyRiskItem } from './types/risk'
import { sortStrategiesByRisk, riskBarCount, riskLevelClass, riskLevelLabel, strategyTypeLabel } from './hooks/useRiskSort'

const props = defineProps<{
  strategies: StrategyRiskItem[]
}>()

const emit = defineEmits<{
  'row-click': [strategy: StrategyRiskItem]
  'config-click': [strategy: StrategyRiskItem]
}>()

const sortedStrategies = computed(() => sortStrategiesByRisk(props.strategies))

function onRowClick(row: StrategyRiskItem) {
  emit('row-click', row)
}

function onConfigClick(row: StrategyRiskItem) {
  emit('config-click', row)
}

function getRankClass(index: number): string {
  if (index === 0) return 'rank-1'
  if (index === 1) return 'rank-2'
  if (index === 2) return 'rank-3'
  return ''
}
</script>

<style scoped lang="scss">
.strategy-risk-list {
  width: 100%;
  overflow-x: auto;
  background: var(--panel-bg);
  border-radius: 10px;
  border: 1px solid var(--border);
  padding: 16px;
  box-shadow: 0 4px 12px rgba(0, 0, 0, 0.15);
}

.risk-table {
  width: 100%;
  border-collapse: collapse;
  font-size: 14px;
  table-layout: fixed;
  color: var(--text);

  th, td {
    padding: 12px 8px;
    text-align: left;
    border-bottom: 1px solid var(--border);
  }

  th {
    background: var(--darker-bg);
    color: var(--text-secondary);
    font-weight: 600;
    font-size: 13px;
    text-transform: uppercase;
    letter-spacing: 0.5px;
    position: sticky;
    top: 0;
    z-index: 1;
  }

  tbody tr {
    transition: background 0.2s;
    cursor: pointer;

    &:hover {
      background: rgba(41, 98, 255, 0.15);
    }

    &.is-stripe {
      background: rgba(0, 0, 0, 0.15);
      
      &:hover {
        background: rgba(41, 98, 255, 0.15);
      }
    }
  }

  // 列宽定义
  .col-rank { width: 60px; text-align: center; }
  .col-name { min-width: 180px; }
  .col-type { width: 80px; text-align: center; }
  .col-risk { width: 140px; }
  .col-var { width: 100px; text-align: right; }
  .col-drawdown { width: 100px; text-align: right; }
  .col-sharpe { width: 100px; text-align: right; }
  .col-winrate { width: 80px; text-align: right; }
  .col-action { width: 60px; text-align: center; }
}

.rank-badge {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  width: 24px;
  height: 24px;
  border-radius: 50%;
  font-size: 12px;
  font-weight: bold;
  background: var(--border);
  color: var(--text-secondary);

  &.rank-1 {
    background: #ff5252;
    color: #fff;
  }

  &.rank-2 {
    background: #ff6d00;
    color: #fff;
  }

  &.rank-3 {
    background: var(--text-secondary);
    color: #fff;
  }
}

.strategy-name {
  font-weight: 500;
  color: var(--text);
}

.strategy-desc {
  font-size: 12px;
  color: var(--text-secondary);
  margin-top: 2px;
}

.type-tag {
  display: inline-block;
  padding: 2px 8px;
  font-size: 12px;
  background: rgba(41, 98, 255, 0.15);
  border: 1px solid var(--border);
  border-radius: 4px;
  color: var(--text);
}

.risk-level-cell {
  display: flex;
  align-items: center;
  gap: 8px;
}

.risk-bars {
  display: flex;
  gap: 2px;

  .risk-bar {
    width: 6px;
    height: 16px;
    border-radius: 2px;
    background: var(--border);
    transition: background 0.2s;

    &.active {
      background: #ff5252;
    }
  }
}

// 风险等级颜色
.risk-critical {
  color: #ff5252;
  font-weight: 600;
}
.risk-high {
  color: #ff6d00;
  font-weight: 600;
}
.risk-medium {
  color: var(--text-secondary);
}
.risk-low {
  color: var(--secondary);
}

.negative {
  color: #ff5252;
}

// 行背景色增强
.risk-row-critical {
  background: rgba(255, 82, 82, 0.08);
  
  &:hover {
    background: rgba(255, 82, 82, 0.15);
  }
}
.risk-row-high {
  background: rgba(255, 109, 0, 0.06);
  
  &:hover {
    background: rgba(255, 109, 0, 0.12);
  }
}

.config-btn {
  border: none;
  background: transparent;
  cursor: pointer;
  padding: 4px 8px;
  border-radius: 4px;
  color: var(--text-secondary);
  transition: all 0.2s;

  &:hover {
    background: rgba(41, 98, 255, 0.15);
    color: var(--primary);
  }

  i {
    font-size: 14px;
  }
}
</style>
