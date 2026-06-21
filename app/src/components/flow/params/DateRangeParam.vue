<!-- src/components/flow/params/DateRangeParam.vue -->
<template>
  <div class="date-range-group">
    <div class="date-range-inputs">
      <input
        type="date"
        :value="value[0]"
        @input="updateDateRange(0, ($event.target as HTMLInputElement).value)"
        @mousedown.stop
        @dragstart.stop
        class="param-input param-date"
        title="开始日期"
      />
      <span class="date-range-separator">至</span>
      <input
        type="date"
        :value="value[1]"
        @input="updateDateRange(1, ($event.target as HTMLInputElement).value)"
        @mousedown.stop
        @dragstart.stop
        class="param-input param-date"
        title="结束日期"
      />
    </div>
    <span v-if="paramConfig?.unit" class="param-unit">{{ paramConfig.unit }}</span>
  </div>
</template>

<script setup lang="ts">
const props = defineProps<{
  paramKey: string
  paramConfig: any
  value: any
}>()

const emit = defineEmits<{
  update: [value: any]
}>()

function updateDateRange(index: number, newDateValue: string) {
  const currentValue = [...props.value]
  currentValue[index] = newDateValue

  if (index === 0 && currentValue[1] && newDateValue > currentValue[1]) {
    currentValue[1] = newDateValue
  } else if (index === 1 && currentValue[0] && newDateValue < currentValue[0]) {
    currentValue[0] = newDateValue
  }

  emit('update', currentValue)
}
</script>

<style scoped>
.date-range-group {
  display: flex;
  align-items: center;
  gap: 6px;
  width: 100%;
}

.date-range-inputs {
  display: flex;
  align-items: center;
  gap: 8px;
  flex: 1;
}

.date-range-separator {
  color: var(--text-secondary);
  font-size: 0.75rem;
  white-space: nowrap;
}

/* 强制暗色主题样式 */
.param-input {
  background: rgba(0, 0, 0, 0.3) !important;
  border: 1px solid var(--border) !important;
  border-radius: 6px !important;
  color: var(--text) !important;
  padding: 5px 10px !important;
  font-size: 0.8rem !important;
  outline: none !important;
  transition: all 0.2s ease !important;
  width: 100% !important;
  color-scheme: dark !important;
}

.param-input:focus {
  border-color: var(--primary) !important;
  box-shadow: 0 0 0 2px rgba(41, 98, 255, 0.2) !important;
  background: rgba(0, 0, 0, 0.4) !important;
}

.param-input:hover:not(:focus) {
  border-color: rgba(41, 98, 255, 0.5) !important;
}

.param-input::-webkit-calendar-picker-indicator {
  filter: invert(0.7) !important;
  cursor: pointer !important;
}
</style>
