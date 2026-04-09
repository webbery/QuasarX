<template>
  <div class="strategy-risk-list">
    <el-table
      :data="sortedStrategies"
      @row-click="onRowClick"
      class="risk-table"
      :row-class-name="rowClassName"
      stripe
    >
      <!-- 排名 -->
      <el-table-column label="排名" width="60" fixed>
        <template #default="{ $index }">
          <span class="rank-badge" :class="getRankClass($index)">
            {{ $index + 1 }}
          </span>
        </template>
      </el-table-column>

      <!-- 策略名称 -->
      <el-table-column label="策略名称" min-width="180">
        <template #default="{ row }">
          <div class="strategy-name">{{ row.name }}</div>
          <div class="strategy-desc">{{ row.description }}</div>
        </template>
      </el-table-column>

      <!-- 类型 -->
      <el-table-column label="类型" width="80">
        <template #default="{ row }">
          <el-tag size="small" effect="plain">{{ strategyTypeLabel(row.strategyType) }}</el-tag>
        </template>
      </el-table-column>

      <!-- 风险等级 -->
      <el-table-column label="风险等级" width="140">
        <template #default="{ row }">
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
        </template>
      </el-table-column>

      <!-- VaR(95%) -->
      <el-table-column label="VaR(95%)" width="100" align="right">
        <template #default="{ row }">
          <span>{{ row.var_95.toFixed(1) }}%</span>
        </template>
      </el-table-column>

      <!-- 最大回撤 -->
      <el-table-column label="最大回撤" width="100" align="right">
        <template #default="{ row }">
          <span class="negative">{{ row.maxDrawdown.toFixed(1) }}%</span>
        </template>
      </el-table-column>

      <!-- 夏普比率 -->
      <el-table-column label="夏普比率" width="100" align="right">
        <template #default="{ row }">
          <span>{{ row.sharpeRatio.toFixed(2) }}</span>
        </template>
      </el-table-column>

      <!-- 胜率 -->
      <el-table-column label="胜率" width="80" align="right">
        <template #default="{ row }">
          <span>{{ row.winRate }}%</span>
        </template>
      </el-table-column>

      <!-- 操作 -->
      <el-table-column label="操作" width="60" fixed="right">
        <template #default="{ row }">
          <el-button text size="small" @click.stop="onConfigClick(row)">
            <i class="fas fa-cog"></i>
          </el-button>
        </template>
      </el-table-column>
    </el-table>
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

function rowClassName({ row }: { row: StrategyRiskItem }): string {
  return `risk-row-${row.riskLevel}`
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
}

.risk-table {
  cursor: pointer;

  :deep(.el-table__row) {
    transition: background 0.15s;

    &:hover {
      background: var(--el-fill-color-light) !important;
    }
  }
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
  background: var(--el-fill-color);
  color: var(--el-text-color-regular);

  &.rank-1 {
    background: #f56c6c;
    color: #fff;
  }

  &.rank-2 {
    background: #e6a23c;
    color: #fff;
  }

  &.rank-3 {
    background: #909399;
    color: #fff;
  }
}

.strategy-name {
  font-weight: 500;
  color: var(--el-text-color-primary);
}

.strategy-desc {
  font-size: 12px;
  color: var(--el-text-color-secondary);
  margin-top: 2px;
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
    background: var(--el-fill-color);
    transition: background 0.2s;

    &.active {
      background: #f56c6c;
    }
  }
}

// 风险等级颜色
:deep(.risk-critical) {
  color: #f56c6c;
  font-weight: 500;
}
:deep(.risk-high) {
  color: #e6a23c;
  font-weight: 500;
}
:deep(.risk-medium) {
  color: #909399;
}
:deep(.risk-low) {
  color: #67c23a;
}

.negative {
  color: #f56c6c;
}

// 行背景色增强
:deep(.risk-row-critical) {
  background: rgba(245, 108, 108, 0.04);
}
:deep(.risk-row-high) {
  background: rgba(230, 162, 60, 0.03);
}
</style>
