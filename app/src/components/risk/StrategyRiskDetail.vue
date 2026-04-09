<template>
  <div class="strategy-risk-detail">
    <div class="detail-header">
      <el-button @click="$emit('back')" class="back-btn" size="default">
        <i class="fas fa-arrow-left"></i> 返回
      </el-button>
      <h2>{{ strategy?.name }} - 风险详情</h2>
      <el-tag
        v-if="strategy"
        :type="riskLevelTagType(strategy.riskLevel)"
        size="large"
        effect="dark"
      >
        {{ riskLevelLabel(strategy.riskLevel) }}
      </el-tag>
    </div>

    <template v-if="strategy">
      <!-- 通用风险指标 -->
      <CommonRiskPanel
        :metrics="{
          var_95: strategy.var_95,
          maxDrawdown: strategy.maxDrawdown,
          sharpeRatio: strategy.sharpeRatio,
          winRate: strategy.winRate,
        }"
      />

      <!-- 期权特有风险 -->
      <OptionRiskPanel
        v-if="strategy.strategyType === 'option' && optionData"
        :data="optionData"
      />

      <!-- 股票特有风险 -->
      <StockRiskPanel
        v-else-if="strategy.strategyType === 'stock' && stockData"
        :data="stockData"
      />

      <!-- 期货特有风险 -->
      <FutureRiskPanel
        v-else-if="strategy.strategyType === 'future' && futureData"
        :data="futureData"
      />
    </template>
  </div>
</template>

<script setup lang="ts">
import type { StrategyRiskItem } from './types/risk'
import type { OptionRiskData, StockRiskData, FutureRiskData } from './types/risk'
import CommonRiskPanel from './panels/CommonRiskPanel.vue'
import OptionRiskPanel from './panels/OptionRiskPanel.vue'
import StockRiskPanel from './panels/StockRiskPanel.vue'
import FutureRiskPanel from './panels/FutureRiskPanel.vue'
import { riskLevelTagType, riskLevelLabel } from './hooks/useRiskSort'

defineProps<{
  strategy: StrategyRiskItem | null
  optionData: OptionRiskData | null
  stockData: StockRiskData | null
  futureData: FutureRiskData | null
}>()

defineEmits<{
  back: []
}>()
</script>

<style scoped lang="scss">
.strategy-risk-detail {
  padding: 16px;
}

.detail-header {
  display: flex;
  align-items: center;
  gap: 16px;
  margin-bottom: 20px;
  padding-bottom: 12px;
  border-bottom: 1px solid var(--el-border-color-lighter);

  .back-btn {
    flex-shrink: 0;
  }

  h2 {
    margin: 0;
    font-size: 18px;
    flex: 1;
    color: var(--el-text-color-primary);
  }
}
</style>
