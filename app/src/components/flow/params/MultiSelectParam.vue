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

<style scoped>
.checkbox-group {
  display: flex;
  flex-wrap: wrap;
  gap: 6px;
}

.checkbox-item {
  display: flex;
  align-items: center;
  gap: 4px;
}

.checkbox-item input[type="checkbox"] {
  appearance: none;
  -webkit-appearance: none;
  width: 14px;
  height: 14px;
  border: 1px solid var(--border, rgba(74, 158, 255, 0.3));
  border-radius: 3px;
  background: rgba(0, 0, 0, 0.3);
  cursor: pointer;
  position: relative;
  transition: all 0.15s;
}

.checkbox-item input[type="checkbox"]:checked {
  background: rgba(41, 98, 255, 0.3);
  border-color: var(--primary, #2962ff);
}

.checkbox-item input[type="checkbox"]:checked::after {
  content: '✓';
  position: absolute;
  top: 50%;
  left: 50%;
  transform: translate(-50%, -50%);
  color: var(--text, #e2e8f0);
  font-size: 10px;
  font-weight: bold;
}

.checkbox-item input[type="checkbox"]:hover {
  border-color: rgba(41, 98, 255, 0.5);
}

.checkbox-item label {
  font-size: 0.75rem;
  color: var(--text-secondary, #94a3b8);
  cursor: pointer;
  user-select: none;
}
</style>
