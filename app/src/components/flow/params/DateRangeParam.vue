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
    <span v-if="paramConfig.unit" class="param-unit">{{ paramConfig.unit }}</span>
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
