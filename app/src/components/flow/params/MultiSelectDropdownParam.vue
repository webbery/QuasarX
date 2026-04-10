<!-- src/components/flow/params/MultiSelectDropdownParam.vue -->
<template>
  <div class="multiselect-dropdown-wrapper">
    <div class="multiselect-dropdown" @click="$emit('toggleDropdown', paramKey)">
      <div class="selected-options-display">
        <template v-if="getSelectedOptions(value, paramConfig.options).length > 0">
          <span class="selected-option-tag"
                v-for="selectedOption in getSelectedOptions(value, paramConfig.options)"
                :key="selectedOption">
            {{ selectedOption }}
          </span>
        </template>
        <span v-else class="placeholder">请选择...</span>
      </div>
      <div class="dropdown-arrow" :class="{ 'open': isOpen }">
        <i class="fas fa-chevron-down"></i>
      </div>
    </div>

    <div v-if="isOpen" class="dropdown-options" @click.stop>
      <div class="dropdown-option"
           v-for="option in paramConfig.options"
           :key="option"
           @click="$emit('toggleOption', option)">
        <input type="checkbox"
               :checked="isSelected(option)"
               @change.stop />
        <span class="option-label">{{ option }}</span>
      </div>
    </div>
    <span v-if="paramConfig.unit" class="param-unit">{{ paramConfig.unit }}</span>
  </div>
</template>

<script setup lang="ts">
import { computed } from 'vue'

const props = defineProps<{
  paramKey: string
  paramConfig: any
  value: any
  isOpen: boolean
}>()

const emit = defineEmits<{
  toggleDropdown: [paramKey: string]
  toggleOption: [option: string]
}>()

function getSelectedOptions(currentValue: any, allOptions: string[]) {
  if (!currentValue || !Array.isArray(currentValue)) return []
  return allOptions.filter(option => currentValue.includes(option))
}

function isSelected(option: string) {
  if (!props.value || !Array.isArray(props.value)) return false
  return props.value.includes(option)
}
</script>
