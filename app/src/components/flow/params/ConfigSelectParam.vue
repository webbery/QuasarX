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
