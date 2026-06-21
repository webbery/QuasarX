<!-- src/components/flow/params/SelectParam.vue -->
<template>
  <div class="input-with-unit">
    <select
      :value="value"
      @change="$emit('update', ($event.target as HTMLSelectElement).value)"
      class="param-input param-select"
    >
      <option
        v-for="option in paramConfig.options"
        :key="getOptionValue(option)"
        :value="getOptionValue(option)"
      >
        {{ getOptionLabel(option) }}
      </option>
    </select>
    <span v-if="paramConfig.unit" class="param-unit">{{ paramConfig.unit }}</span>
  </div>
</template>

<script setup lang="ts">
defineProps<{
  paramKey: string
  paramConfig: any
  value: any
}>()

defineEmits<{
  update: [value: any]
}>()

function getOptionValue(option: any) {
  return typeof option === 'object' && option !== null ? option.value : option
}

function getOptionLabel(option: any) {
  return typeof option === 'object' && option !== null ? option.label : option
}
</script>

<style scoped>
/* 强制暗色主题样式 - select 下拉框 */
.param-input {
  background: rgba(0, 0, 0, 0.3) !important;
  border: 1px solid var(--border) !important;
  border-radius: 6px !important;
  color: var(--text) !important;
  padding: 5px 10px !important;
  font-size: 0.8rem !important;
  outline: none !important;
  cursor: pointer !important;
  transition: all 0.2s ease !important;
  width: 100% !important;
  appearance: none !important;
  -webkit-appearance: none !important;
  background-image: url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' width='12' height='12' viewBox='0 0 12 12'%3E%3Cpath fill='%23a0aec0' d='M6 8L1 3h10z'/%3E%3C/svg%3E") !important;
  background-repeat: no-repeat !important;
  background-position: right 8px center !important;
  padding-right: 28px !important;
}

.param-input:focus {
  border-color: var(--primary) !important;
  box-shadow: 0 0 0 2px rgba(41, 98, 255, 0.2) !important;
  background: rgba(0, 0, 0, 0.4) !important;
}

.param-input:hover:not(:focus) {
  border-color: rgba(41, 98, 255, 0.5) !important;
}

/* option 选项样式 - 注意：scoped 样式对 option 元素可能不生效 */
/* 需要在 StrategyNode.vue 中使用 :deep() 或全局样式 */
.param-input option {
  background: var(--panel-bg) !important;
  color: var(--text) !important;
  padding: 8px !important;
}

/* 针对原生 select 元素的 option */
select option {
  background: var(--panel-bg) !important;
  color: var(--text) !important;
}
</style>
