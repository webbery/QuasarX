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
  background: rgba(0, 0, 0, 0.3);
  border: 1px solid var(--border, rgba(74, 158, 255, 0.3));
  border-radius: 6px;
  color: var(--text, #e2e8f0);
  padding: 5px 10px;
  font-size: 0.8rem;
  outline: none;
  cursor: pointer;
  width: 100%;
}

.config-select-wrapper .param-input option {
  background: var(--panel-bg, #1a2236);
  color: var(--text, #e2e8f0);
}

.config-summary-card {
  margin-top: 6px;
  padding: 6px 8px;
  background: rgba(0, 0, 0, 0.2);
  border: 1px solid var(--border, rgba(74, 158, 255, 0.15));
  border-radius: 4px;
  font-size: 0.7rem;
}

.summary-row {
  display: flex;
  justify-content: space-between;
  padding: 2px 0;
}

.summary-label {
  color: var(--text-secondary, #94a3b8);
}

.summary-value {
  color: var(--text, #e2e8f0);
  font-weight: 500;
}

.summary-value.positive {
  color: #4ade80;
}

.create-config-btn {
  display: block;
  width: 100%;
  margin-top: 6px;
  padding: 4px 8px;
  background: rgba(74, 222, 128, 0.1);
  border: 1px solid rgba(74, 222, 128, 0.3);
  border-radius: 4px;
  color: var(--text, #e2e8f0);
  font-size: 0.7rem;
  cursor: pointer;
  text-align: center;
  transition: all 0.15s;
}

.create-config-btn:hover {
  background: rgba(74, 222, 128, 0.2);
  border-color: rgba(74, 222, 128, 0.5);
}
</style>
