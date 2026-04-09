<template>
  <el-drawer
    v-model="visible"
    title="风险监控配置"
    size="480px"
    :close-on-click-modal="true"
  >
    <div v-if="config" class="risk-config-drawer">
      <!-- 策略信息 -->
      <div class="config-header">
        <h4>{{ strategy?.name }}</h4>
        <el-switch
          v-model="config.enabled"
          active-text="监控中"
          inactive-text="已停用"
        />
      </div>

      <!-- 监控指标列表 -->
      <div class="metric-list">
        <div
          v-for="(metric, idx) in config.metrics"
          :key="metric.key"
          class="metric-item"
        >
          <div class="metric-header">
            <el-checkbox v-model="metric.enabled" :label="metric.label" />
            <span class="metric-unit">{{ metric.unit }}</span>
          </div>
          <div class="metric-thresholds">
            <div class="threshold-group">
              <label>预警阈值</label>
              <el-input-number
                v-model="metric.warningThreshold"
                :disabled="!metric.enabled"
                size="small"
                :precision="2"
                :step="0.1"
                :controls="false"
                class="threshold-input"
              />
            </div>
            <div class="threshold-group">
              <label>危险阈值</label>
              <el-input-number
                v-model="metric.criticalThreshold"
                :disabled="!metric.enabled"
                size="small"
                :precision="2"
                :step="0.1"
                :controls="false"
                class="threshold-input"
              />
            </div>
          </div>
        </div>
      </div>
    </div>

    <template #footer>
      <div class="drawer-footer">
        <el-button @click="visible = false">取消</el-button>
        <el-button type="primary" @click="handleSave">保存</el-button>
      </div>
    </template>
  </el-drawer>
</template>

<script setup lang="ts">
import { ref, computed, watch } from 'vue'
import type { StrategyRiskItem, StrategyRiskConfig, RiskMetricConfig } from './types/risk'

const props = defineProps<{
  modelValue: boolean
  strategy: StrategyRiskItem | null
}>()

const emit = defineEmits<{
  'update:modelValue': [value: boolean]
  save: [config: StrategyRiskConfig]
}>()

const visible = computed({
  get: () => props.modelValue,
  set: (v: boolean) => emit('update:modelValue', v),
})

// 默认指标配置
function getDefaultMetrics(): RiskMetricConfig[] {
  return [
    { key: 'var_95', label: 'VaR (95%)', enabled: true, warningThreshold: 3.0, criticalThreshold: 5.0, unit: '%' },
    { key: 'maxDrawdown', label: '最大回撤', enabled: true, warningThreshold: 8.0, criticalThreshold: 12.0, unit: '%' },
    { key: 'sharpeRatio', label: '夏普比率', enabled: true, warningThreshold: 1.0, criticalThreshold: 0.5, unit: '' },
    { key: 'winRate', label: '胜率', enabled: false, warningThreshold: 50, criticalThreshold: 40, unit: '%' },
  ]
}

const config = ref<StrategyRiskConfig | null>(null)

watch(() => props.strategy, (s) => {
  if (s) {
    config.value = {
      strategyId: s.id,
      enabled: true,
      metrics: getDefaultMetrics(),
    }
  } else {
    config.value = null
  }
}, { immediate: true })

function handleSave() {
  if (config.value) {
    emit('save', config.value)
    visible.value = false
  }
}
</script>

<style scoped lang="scss">
.risk-config-drawer {
  padding: 0 8px;
}

.config-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 12px 0;
  margin-bottom: 16px;
  border-bottom: 1px solid var(--el-border-color-lighter);

  h4 {
    margin: 0;
    font-size: 16px;
  }
}

.metric-list {
  display: flex;
  flex-direction: column;
  gap: 16px;
}

.metric-item {
  background: var(--el-fill-color-lighter);
  border: 1px solid var(--el-border-color-lighter);
  border-radius: 6px;
  padding: 12px;

  .metric-header {
    display: flex;
    justify-content: space-between;
    align-items: center;
    margin-bottom: 10px;

    .metric-unit {
      font-size: 12px;
      color: var(--el-text-color-placeholder);
    }
  }

  .metric-thresholds {
    display: grid;
    grid-template-columns: 1fr 1fr;
    gap: 12px;

    .threshold-group {
      label {
        display: block;
        font-size: 12px;
        color: var(--el-text-color-secondary);
        margin-bottom: 4px;
      }
    }
  }

  .threshold-input {
    width: 100%;
  }
}

.drawer-footer {
  display: flex;
  justify-content: flex-end;
  gap: 8px;
}
</style>
