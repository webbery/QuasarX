<!-- src/components/flow/params/MultiSelectParam.vue -->
<template>
  <div class="checkbox-group">
    <div
      v-for="option in paramConfig.options"
      :key="option"
      class="checkbox-item"
    >
      <input
        type="checkbox"
        :id="`${nodeId}-${paramKey}-${option}`"
        :value="option"
        :checked="isChecked(option)"
        @change="onCheckboxChange(option, $event)"
        @mousedown.stop
        @dragstart.stop
      />
      <label :for="`${nodeId}-${paramKey}-${option}`"
          @mousedown.stop
          @dragstart.stop
      >{{ option }}</label>
    </div>
  </div>
</template>

<script setup lang="ts">
const props = defineProps<{
  paramKey: string
  paramConfig: any
  value: any
  nodeId: string
}>()

const emit = defineEmits<{
  update: [value: any]
}>()

function isChecked(option: string) {
  if (!props.value || !Array.isArray(props.value)) return false
  return props.value.includes(option)
}

function onCheckboxChange(option: string, event: Event) {
  const target = event.target as HTMLInputElement
  const currentValue = props.value || []
  let newValue

  if (target.checked) {
    newValue = [...currentValue, option]
  } else {
    newValue = currentValue.filter((item: string) => item !== option)
  }

  emit('update', newValue)
}
</script>
