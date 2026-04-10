<!-- src/components/flow/params/DateParam.vue -->
<template>
  <div class="input-with-unit">
    <input
      type="date"
      :value="value"
      @input="$emit('update', ($event.target as HTMLInputElement).value)"
      @mousedown.stop
      @dragstart.stop
      class="param-input param-date"
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
