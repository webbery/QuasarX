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
