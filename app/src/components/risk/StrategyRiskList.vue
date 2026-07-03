<template>
  <div class="strategy-risk-list">
    <table class="risk-table">
      <thead>
        <tr>
          <th class="col-name">策略名称</th>
          <th class="col-type">类型</th>
          <th class="col-risk">风险等级</th>
          <th class="col-var">VaR(95%)</th>
          <th class="col-drawdown">最大回撤</th>
          <th class="col-sharpe">夏普比率</th>
          <th class="col-ir">信息比率</th>
          <th class="col-health">健康度</th>
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
          <!-- 策略名称 -->
          <td class="col-name">
            <div class="strategy-name">{{ row.name }}</div>
            <div class="strategy-desc">{{ row.description }}</div>
          </td>

          <!-- 类型 -->
          <td class="col-type">
            <span class="type-tag">{{ typeLabel(row.strategyType) }}</span>
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
            <span>{{ (row.var_95 || 0).toFixed(1) }}%</span>
          </td>

          <!-- 最大回撤 -->
          <td class="col-drawdown">
            <span class="negative">{{ (row.maxDrawdown || 0).toFixed(1) }}%</span>
          </td>

          <!-- 夏普比率 -->
          <td class="col-sharpe">
            <span>{{ (row.sharpeRatio || 0).toFixed(2) }}</span>
          </td>

          <!-- 信息比率 (IR) -->
          <td class="col-ir">
            <span :class="irClass(row.informationRatio)">{{ (row.informationRatio || 0).toFixed(2) }}</span>
          </td>

          <!-- 健康度 -->
          <td class="col-health">
            <div class="health-cell">
              <span class="health-icon">{{ healthIcon(row.healthLevel) }}</span>
              <span :class="healthClass(row.healthLevel)">{{ healthLabel(row.healthLevel) }}</span>
              <span class="health-suggestion">{{ row.healthSuggestion }}</span>
            </div>
          </td>

          <!-- 胜率 -->
          <td class="col-winrate">
            <span>{{ (row.winRate || 0).toFixed(1) }}%</span>
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
import { riskBarCount, riskLevelClass, riskLevelLabel } from './hooks/useRiskSort'

const props = defineProps<{
  strategies: StrategyRiskItem[]
}>()

const emit = defineEmits<{
  'row-click': [strategy: StrategyRiskItem]
  'config-click': [strategy: StrategyRiskItem]
}>()

const sortedStrategies = computed(() => {
  // 按风险分数降序排列
  return [...props.strategies].sort((a, b) => (b.riskScore || 0) - (a.riskScore || 0))
})

function onRowClick(row: StrategyRiskItem) {
  emit('row-click', row)
}

function onConfigClick(row: StrategyRiskItem) {
  emit('config-click', row)
}

// 标的类型标签
function typeLabel(type: string): string {
  const labels: Record<string, string> = {
    stock: '股票',
    etf: 'ETF',
    future: '期货',
    option: '期权',
    mixed: '混合',
  }
  return labels[type] || type
}

// IR 颜色
function irClass(ir: number): string {
  if (ir < 0) return 'ir-negative'
  if (ir >= 1) return 'ir-excellent'
  if (ir >= 0.5) return 'ir-good'
  return 'ir-warning'
}

// 健康度图标
function healthIcon(level: string): string {
  const icons: Record<string, string> = {
    excellent: '🟢',
    good: '🔵',
    warning: '🟠',
    critical: '🔴',
  }
  return icons[level] || '⚪'
}

// 健康度标签
function healthLabel(level: string): string {
  const labels: Record<string, string> = {
    excellent: '优秀',
    good: '良好',
    warning: '警戒',
    critical: '恶化',
  }
  return labels[level] || '未知'
}

// 健康度颜色
function healthClass(level: string): string {
  return `health-${level}`
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
  .col-name { min-width: 180px; }
  .col-type { width: 80px; text-align: center; }
  .col-risk { width: 140px; }
  .col-var { width: 90px; text-align: right; }
  .col-drawdown { width: 90px; text-align: right; }
  .col-sharpe { width: 90px; text-align: right; }
  .col-ir { width: 90px; text-align: right; }
  .col-health { width: 140px; }
  .col-winrate { width: 80px; text-align: right; }
  .col-action { width: 60px; text-align: center; }
}

// IR 颜色
.ir-negative { color: #ff1744; font-weight: 600; }
.ir-excellent { color: #00c853; font-weight: 600; }
.ir-good { color: #2962ff; }
.ir-warning { color: #ff9800; }

// 健康度单元格
.health-cell {
  display: flex;
  align-items: center;
  gap: 4px;
}

.health-icon { font-size: 14px; }

.health-excellent { color: #00c853; font-weight: 600; }
.health-good { color: #2962ff; font-weight: 600; }
.health-warning { color: #ff9800; font-weight: 600; }
.health-critical { color: #ff1744; font-weight: 600; }

.health-suggestion {
  font-size: 11px;
  color: var(--text-secondary);
  margin-left: 2px;
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
