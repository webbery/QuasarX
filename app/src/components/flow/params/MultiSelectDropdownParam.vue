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

<style scoped>
.multiselect-dropdown-wrapper {
  position: relative;
  width: 100%;
}

.multiselect-dropdown {
  background: rgba(0, 0, 0, 0.3);
  border: 1px solid var(--border, rgba(74, 158, 255, 0.3));
  border-radius: 6px;
  padding: 4px 28px 4px 8px;
  min-height: 28px;
  cursor: pointer;
  transition: all 0.2s ease;
  display: flex;
  align-items: center;
  flex-wrap: wrap;
  gap: 4px;
}

.multiselect-dropdown:hover {
  border-color: rgba(41, 98, 255, 0.5);
}

.selected-options-display {
  flex: 1;
  display: flex;
  flex-wrap: wrap;
  gap: 4px;
}

.selected-option-tag {
  background: rgba(41, 98, 255, 0.2);
  color: var(--text, #e2e8f0);
  padding: 1px 6px;
  border-radius: 4px;
  font-size: 0.7rem;
}

.placeholder {
  color: var(--text-secondary, #64748b);
  font-size: 0.75rem;
}

.dropdown-arrow {
  position: absolute;
  right: 8px;
  top: 50%;
  transform: translateY(-50%);
  color: var(--text-secondary, #94a3b8);
  font-size: 0.7rem;
  transition: transform 0.2s;
}

.dropdown-options {
  position: absolute;
  top: 100%;
  left: 0;
  right: 0;
  z-index: 100;
  background: rgba(15, 23, 42, 0.95);
  border: 1px solid var(--border, rgba(74, 158, 255, 0.3));
  border-radius: 6px;
  max-height: 200px;
  overflow-y: auto;
  margin-top: 4px;
  box-shadow: 0 4px 12px rgba(0, 0, 0, 0.4);
}

.dropdown-option {
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 6px 10px;
  cursor: pointer;
  transition: background 0.15s;
}

.dropdown-option:hover {
  background: rgba(41, 98, 255, 0.1);
}

.dropdown-option input[type="checkbox"] {
  appearance: none;
  -webkit-appearance: none;
  width: 14px;
  height: 14px;
  border: 1px solid var(--border, rgba(74, 158, 255, 0.3));
  border-radius: 3px;
  background: rgba(0, 0, 0, 0.3);
  cursor: pointer;
  position: relative;
  flex-shrink: 0;
}

.dropdown-option input[type="checkbox"]:checked {
  background: rgba(41, 98, 255, 0.3);
  border-color: var(--primary, #2962ff);
}

.dropdown-option input[type="checkbox"]:checked::after {
  content: '✓';
  position: absolute;
  top: 50%;
  left: 50%;
  transform: translate(-50%, -50%);
  color: var(--text, #e2e8f0);
  font-size: 10px;
  font-weight: bold;
}

.option-label {
  color: var(--text, #e2e8f0);
  font-size: 0.8rem;
}
</style>
