<template>
  <div class="risk-view">
    <!-- 市场风险概览 -->
    <MarketRiskOverview
      v-model:market-type="selectedMarket"
      :data="marketData"
    />

    <!-- 全局风控配置（可折叠） -->
    <GlobalRiskConfig />

    <!-- 策略风险列表 -->
    <div class="strategy-list-section">
      <div class="section-header">
        <h3><i class="fas fa-shield-alt"></i> 策略风险监控</h3>
      </div>
      <StrategyRiskList
        :strategies="strategies"
        @row-click="onStrategyClick"
      />
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, onMounted, onActivated } from 'vue'
import axios from 'axios'
import type { MarketType, StrategyRiskItem, MarketRiskData } from './types/risk'
import { assessHealth } from './hooks/useHealthAssess'
import MarketRiskOverview from './MarketRiskOverview.vue'
import StrategyRiskList from './StrategyRiskList.vue'
import GlobalRiskConfig from './GlobalRiskConfig.vue'

const emit = defineEmits<{
  'strategy-click': [strategy: StrategyRiskItem]
}>()

// 市场数据（待后续接入实时行情）
const selectedMarket = ref<MarketType>('astock')
const marketData = ref<MarketRiskData>({
  marketType: 'astock',
  indexName: '上证指数',
  indexValue: 0,
  changePercent: 0,
  volatility: 0,
  advanceCount: 0,
  declineCount: 0,
  sentimentIndex: 0,
  sentimentLabel: '',
})

// 策略风险数据（从后端 API 获取）
const strategies = ref<StrategyRiskItem[]>([])

// 获取策略风险数据
async function fetchStrategies() {
  try {
    const { data } = await axios.get('/v0/risk/strategies')

    // 后端返回 snake_case，前端需要 camelCase；补全缺失字段
    strategies.value = data.map((item: any) => {
      const health = assessHealth({
        ir: item.information_ratio || 0,
        maxDrawdown: item.max_drawdown || 0,
        varRatio: item.var_95 || 0,
        driftRatio: item.cusum_drift_ratio || 0,
        winRate: item.win_rate || 0,
        avgWinLossRatio: item.avg_win_loss_ratio || 1,
      })

      // 从四维评分推导风险等级
      let riskLevel: 'low' | 'medium' | 'high' | 'critical' = 'low'
      const dd = Math.abs(item.max_drawdown || 0)
      if (dd > 15 || health.level === 'critical') riskLevel = 'critical'
      else if (dd > 10 || health.level === 'warning') riskLevel = 'high'
      else if (dd > 5) riskLevel = 'medium'

      return {
        ...item,
        // snake_case → camelCase 映射
        sharpeRatio: item.sharpe_ratio || 0,
        informationRatio: item.information_ratio || 0,
        maxDrawdown: item.max_drawdown || 0,
        var_95: item.var_95 || 0,
        var_convexity: item.var_convexity || 0,
        winRate: item.win_rate || 0,
        cusumSignal: item.cusum_signal || 0,
        cusumTriggered: item.cusum_triggered || false,
        cusum_drift_ratio: item.cusum_drift_ratio || 0,
        excess_kurtosis: item.excess_kurtosis || 0,
        avg_win_loss_ratio: item.avg_win_loss_ratio || 1,
        // 补全缺失字段
        description: '',
        riskLevel,
        riskScore: health.score,
        strategyType: item.type || 'mixed',
        updatedAt: '',
        // 健康度
        healthLevel: health.level,
        healthSuggestion: health.suggestion,
        healthScore: health.score,
        inferredType: health.strategyType,
      }
    })
  } catch (err) {
    console.error('[RiskView] Fetch strategies error:', err)
  }
}

// KeepAlive 下 onActivated 在首次挂载和后续激活时都会触发
// onMounted 仅在首次创建时触发，用 flag 防止 onActivated 首次重复调用
let initialized = false
onMounted(() => {
  fetchStrategies()
  initialized = true
})

onActivated(() => {
  if (!initialized) return
  fetchStrategies()
})

function onStrategyClick(strategy: StrategyRiskItem) {
  emit('strategy-click', strategy)
}
</script>

<style scoped lang="scss">
.risk-view {
  padding: 20px;
  display: flex;
  flex-direction: column;
  gap: 20px;
  height: 100%;
  overflow-y: auto;
  background: var(--dark-bg);
}

.strategy-list-section {
  background: var(--panel-bg);
  border: 1px solid var(--border);
  border-radius: 10px;
  padding: 20px;
  box-shadow: 0 4px 12px rgba(0, 0, 0, 0.15);

  .section-header {
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
  }
}
</style>
