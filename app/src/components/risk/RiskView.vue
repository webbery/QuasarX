<template>
  <div class="risk-view">
    <!-- 市场风险概览 -->
    <MarketRiskOverview
      v-model:market-type="selectedMarket"
      :data="marketData"
    />

    <!-- 策略风险列表 -->
    <div class="strategy-list-section">
      <div class="section-header">
        <h3><i class="fas fa-shield-alt"></i> 策略风险监控</h3>
        <el-button text @click="openGlobalConfig">
          <i class="fas fa-cog"></i> 全局配置
        </el-button>
      </div>
      <StrategyRiskList
        :strategies="strategies"
        @row-click="onStrategyClick"
        @config-click="onConfigClick"
      />
    </div>

    <!-- 风险配置抽屉 -->
    <RiskConfigDrawer
      v-model="configDrawerVisible"
      :strategy="configTargetStrategy"
      @save="onConfigSave"
    />
  </div>
</template>

<script setup lang="ts">
import { ref, defineEmits } from 'vue'
import type { MarketType, StrategyRiskItem, StrategyRiskConfig } from './types/risk'
import { useMockRiskData } from './hooks/useMockRiskData'
import MarketRiskOverview from './MarketRiskOverview.vue'
import StrategyRiskList from './StrategyRiskList.vue'
import RiskConfigDrawer from './RiskConfigDrawer.vue'

const emit = defineEmits<{
  'strategy-click': [strategy: StrategyRiskItem]
}>()

const {
  selectedMarket,
  marketData,
  strategies,
} = useMockRiskData()

const configDrawerVisible = ref(false)
const configTargetStrategy = ref<StrategyRiskItem | null>(null)

function onStrategyClick(strategy: StrategyRiskItem) {
  emit('strategy-click', strategy)
}

function onConfigClick(strategy: StrategyRiskItem) {
  configTargetStrategy.value = strategy
  configDrawerVisible.value = true
}

function onConfigSave(config: StrategyRiskConfig) {
  console.log('保存风险配置:', config)
  // TODO: 持久化配置
}

function openGlobalConfig() {
  // TODO: 全局配置逻辑
}
</script>

<style scoped lang="scss">
.risk-view {
  padding: 16px;
  display: flex;
  flex-direction: column;
  gap: 16px;
  height: 100%;
  overflow-y: auto;
}

.strategy-list-section {
  background: var(--el-bg-color);
  border: 1px solid var(--el-border-color-light);
  border-radius: 8px;
  padding: 16px;

  .section-header {
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
  }
}
</style>
