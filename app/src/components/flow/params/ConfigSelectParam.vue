<!-- src/components/flow/params/ConfigSelectParam.vue -->
<template>
  <div class="config-select-wrapper">
    <select
      :value="value"
      @change="$emit('update', ($event.target as HTMLSelectElement).value)"
      class="param-input param-select"
    >
      <option value="">-- 选择配置 --</option>
      <option v-for="config in configOptions"
              :key="config.id"
              :value="config.id">
        {{ config.name }} ({{ config.securities?.length || 0 }}个标的)
      </option>
    </select>

    <!-- 配置简介卡片 -->
    <div v-if="selectedConfigSummary" class="config-summary-card">
      <div class="summary-row">
        <span class="summary-label">模型:</span>
        <span class="summary-value">{{ selectedConfigSummary.modelTypeText }}</span>
      </div>
      <div class="summary-row">
        <span class="summary-label">证券数:</span>
        <span class="summary-value">{{ selectedConfigSummary.securities?.length || 0 }}</span>
      </div>
      <div class="summary-row">
        <span class="summary-label">观点数:</span>
        <span class="summary-value">{{ selectedConfigSummary.views?.length || 0 }}</span>
      </div>
      <div class="summary-row" v-if="selectedConfigSummary.optimizationResult">
        <span class="summary-label">预期收益:</span>
        <span class="summary-value positive">{{ selectedConfigSummary.optimizationResult.expectedReturn?.toFixed(2) || '-' }}%</span>
      </div>
    </div>

    <!-- 创建新配置按钮 -->
    <button class="create-config-btn" @click="$emit('openConfigManager')">
      <i class="fas fa-plus"></i> 新建配置
    </button>
  </div>
</template>

<script setup lang="ts">
import { computed } from 'vue'

const props = defineProps<{
  paramKey: string
  paramConfig: any
  value: any
  configOptions: any[]
}>()

defineEmits<{
  update: [value: any]
  openConfigManager: []
}>()

const selectedConfigSummary = computed(() => {
  if (!props.value) return null
  return props.configOptions.find(c => c.id === props.value) || null
})
</script>

<style scoped>
.config-select-wrapper {
  width: 100%;
}

.config-select-wrapper .param-input {
  background: rgba(0, 0, 0, 0.3) !important;
  border: 1px solid var(--border) !important;
  border-radius: 6px !important;
  color: var(--text) !important;
  padding: 5px 10px !important;
  font-size: 0.8rem !important;
  outline: none !important;
  cursor: pointer !important;
  width: 100% !important;
}

.config-select-wrapper .param-input option {
  background: var(--panel-bg) !important;
  color: var(--text) !important;
}

.config-summary-card {
  margin-top: 6px !important;
  padding: 6px 8px !important;
  background: rgba(0, 0, 0, 0.2) !important;
  border: 1px solid var(--border) !important;
  border-radius: 4px !important;
  font-size: 0.7rem !important;
}

.summary-row {
  display: flex !important;
  justify-content: space-between !important;
  padding: 2px 0 !important;
}

.summary-label {
  color: var(--text-secondary) !important;
}

.summary-value {
  color: var(--text) !important;
  font-weight: 500 !important;
}

.summary-value.positive {
  color: #4ade80 !important;
}

.create-config-btn {
  display: block !important;
  width: 100% !important;
  margin-top: 6px !important;
  padding: 4px 8px !important;
  background: rgba(74, 222, 128, 0.1) !important;
  border: 1px solid rgba(74, 222, 128, 0.3) !important;
  border-radius: 4px !important;
  color: var(--text) !important;
  font-size: 0.7rem !important;
  cursor: pointer !important;
  text-align: center !important;
  transition: all 0.15s !important;
}

.create-config-btn:hover {
  background: rgba(74, 222, 128, 0.2) !important;
  border-color: rgba(74, 222, 128, 0.5) !important;
}
</style>
