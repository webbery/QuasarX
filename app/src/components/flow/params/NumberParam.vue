<!-- src/components/flow/params/NumberParam.vue -->
<template>
  <div class="input-with-unit">
    <input
      type="number"
      :value="value"
      @input="$emit('update', ($event.target as HTMLInputElement).value)"
      :min="paramConfig.min"
      :max="paramConfig.max"
      :step="paramConfig.step || 1"
      @mousedown.stop
      @dragstart.stop
      class="param-input param-number"
    />
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
</script>
