<template>
  <div class="risk-view">
    <!-- 市场风险概览 -->
    <MarketRiskOverview
      v-model:market-type="selectedMarket"
      :data="marketData"
    />

    <!-- 回撤断路器仪表盘 -->
    <div class="breaker-section" v-if="breakerStatus">
      <div class="breaker-card">
        <div class="breaker-header">
          <span class="breaker-title">回撤断路器</span>
          <span :class="['breaker-badge', breakerBadgeClass]">{{ breakerStatus.breaker_level }}级</span>
        </div>
        <div class="breaker-bar-container">
          <div class="breaker-bar">
            <div class="breaker-fill" :style="{ width: breakerFillPercent + '%' }"></div>
            <div class="breaker-marker" :style="{ left: breakerFillPercent + '%' }"></div>
          </div>
          <div class="breaker-labels">
            <span class="breaker-label l1" :style="{ left: breakerL1Percent + '%' }">L1 {{ ((breakerStatus.thresholds?.level1 || 0.05) * 100).toFixed(0) }}%</span>
            <span class="breaker-label l2" :style="{ left: breakerL2Percent + '%' }">L2 {{ ((breakerStatus.thresholds?.level2 || 0.10) * 100).toFixed(0) }}%</span>
            <span class="breaker-label l3" :style="{ left: breakerL3Percent + '%' }">L3 {{ ((breakerStatus.thresholds?.level3 || 0.15) * 100).toFixed(0) }}%</span>
          </div>
        </div>
        <div class="breaker-info">
          当前回撤 <strong>{{ (breakerStatus.current_drawdown * 100).toFixed(1) }}%</strong>
          <span v-if="breakerStatus.var_current" class="breaker-var">
            | VaR {{ (breakerStatus.var_current * 100).toFixed(2) }}%
            <span v-if="breakerStatus.var_breached" class="var-breached">超限</span>
          </span>
        </div>
      </div>
    </div>

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
import { ref, computed, defineEmits, onMounted } from 'vue'
import axios from 'axios'
import type { MarketType, StrategyRiskItem, StrategyRiskConfig, MarketRiskData } from './types/risk'
import { assessHealth } from './hooks/useHealthAssess'
import MarketRiskOverview from './MarketRiskOverview.vue'
import StrategyRiskList from './StrategyRiskList.vue'
import RiskConfigDrawer from './RiskConfigDrawer.vue'

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

// 断路器状态
const breakerStatus = ref<any>(null)

const configDrawerVisible = ref(false)
const configTargetStrategy = ref<StrategyRiskItem | null>(null)

// 断路器计算属性
const breakerFillPercent = computed(() => {
  if (!breakerStatus.value) return 0
  const dd = breakerStatus.value.current_drawdown || 0
  const l3 = breakerStatus.value.thresholds?.level3 || 0.15
  return Math.min((dd / l3) * 100, 100)
})

const breakerL1Percent = computed(() => {
  const l1 = breakerStatus.value?.thresholds?.level1 || 0.05
  const l3 = breakerStatus.value?.thresholds?.level3 || 0.15
  return (l1 / l3) * 100
})

const breakerL2Percent = computed(() => {
  const l2 = breakerStatus.value?.thresholds?.level2 || 0.10
  const l3 = breakerStatus.value?.thresholds?.level3 || 0.15
  return (l2 / l3) * 100
})

const breakerL3Percent = computed(() => 100)

const breakerBadgeClass = computed(() => {
  const level = breakerStatus.value?.breaker_level || 0
  if (level >= 3) return 'badge-critical'
  if (level >= 2) return 'badge-high'
  if (level >= 1) return 'badge-warning'
  return 'badge-normal'
})

// 获取策略风险数据
async function fetchStrategies() {
  try {
    const { data } = await axios.get('/v0/risk/strategies')
    
    // 根据四维评分计算健康度
    strategies.value = data.map((item: any) => {
      const health = assessHealth({
        ir: item.information_ratio || 0,
        maxDrawdown: item.max_drawdown || 0,
        varRatio: item.var_95 || 0,
        driftRatio: item.cusum_drift_ratio || 0,
        winRate: item.win_rate || 0,
        avgWinLossRatio: item.avg_win_loss_ratio || 1,
      })
      return {
        ...item,
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

onMounted(() => {
  fetchStrategies()
  fetchBreakerStatus()
})

async function fetchBreakerStatus() {
  try {
    const { data } = await axios.get('/v0/risk/status')
    breakerStatus.value = data
  } catch (err) {
    console.error('[RiskView] Fetch breaker status error:', err)
  }
}

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

    .config-btn {
      background: var(--panel-bg);
      border: 1px solid var(--border);
      border-radius: 6px;
      color: var(--text);
      padding: 8px 16px;
      cursor: pointer;
      transition: all 0.2s;
      font-size: 14px;

      &:hover {
        background: var(--primary);
        border-color: var(--primary);
        color: #fff;
      }

      i {
        margin-right: 6px;
      }
    }
  }
}

// 断路器仪表盘
.breaker-section {
  margin-bottom: 20px;
}

.breaker-card {
  background: var(--panel-bg);
  border: 1px solid var(--border);
  border-radius: 10px;
  padding: 16px 20px;
}

.breaker-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  margin-bottom: 12px;
}

.breaker-title {
  font-size: 14px;
  font-weight: 600;
  color: var(--text);
}

.breaker-badge {
  padding: 2px 10px;
  border-radius: 12px;
  font-size: 12px;
  font-weight: 600;
}

.badge-normal { background: rgba(0, 200, 83, 0.15); color: #00c853; }
.badge-warning { background: rgba(255, 152, 0, 0.15); color: #ff9800; }
.badge-high { background: rgba(255, 82, 82, 0.15); color: #ff5252; }
.badge-critical { background: rgba(255, 23, 68, 0.2); color: #ff1744; }

.breaker-bar-container {
  position: relative;
  margin-bottom: 20px;
}

.breaker-bar {
  height: 8px;
  background: rgba(255, 255, 255, 0.06);
  border-radius: 4px;
  position: relative;
  overflow: hidden;
}

.breaker-fill {
  height: 100%;
  border-radius: 4px;
  background: linear-gradient(90deg, #00c853 0%, #ff9800 60%, #ff1744 100%);
  transition: width 0.5s ease;
}

.breaker-marker {
  position: absolute;
  top: -3px;
  width: 3px;
  height: 14px;
  background: #fff;
  border-radius: 2px;
  transform: translateX(-50%);
  transition: left 0.5s ease;
  box-shadow: 0 0 4px rgba(255, 255, 255, 0.5);
}

.breaker-labels {
  position: relative;
  height: 16px;
  margin-top: 4px;
}

.breaker-label {
  position: absolute;
  transform: translateX(-50%);
  font-size: 11px;
  color: var(--text-secondary);
  white-space: nowrap;
}

.breaker-label.l1 { color: #ff9800; }
.breaker-label.l2 { color: #ff5252; }
.breaker-label.l3 { color: #ff1744; font-weight: 600; }

.breaker-info {
  font-size: 13px;
  color: var(--text-secondary);

  strong { color: var(--text); }
}

.breaker-var {
  margin-left: 4px;
}

.var-breached {
  color: #ff1744;
  font-weight: 600;
  margin-left: 4px;
}
</style>
