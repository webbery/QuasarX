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
  background: rgba(15, 23, 42, 0.98) !important;
  border: 1px solid var(--border) !important;
  border-radius: 6px !important;
  max-height: 200px !important;
  overflow-y: auto !important;
  margin-top: 4px !important;
  box-shadow: 0 4px 12px rgba(0, 0, 0, 0.4) !important;
}

.dropdown-option {
  display: flex !important;
  align-items: center !important;
  gap: 8px !important;
  padding: 6px 10px !important;
  cursor: pointer !important;
  transition: background 0.15s !important;
}

.dropdown-option:hover {
  background: rgba(41, 98, 255, 0.15) !important;
}

.dropdown-option input[type="checkbox"] {
  appearance: none !important;
  -webkit-appearance: none !important;
  width: 14px !important;
  height: 14px !important;
  border: 1px solid var(--border) !important;
  border-radius: 3px !important;
  background: rgba(0, 0, 0, 0.3) !important;
  cursor: pointer !important;
  position: relative !important;
  flex-shrink: 0 !important;
}

.dropdown-option input[type="checkbox"]:checked {
  background: rgba(41, 98, 255, 0.3) !important;
  border-color: var(--primary) !important;
}

.dropdown-option input[type="checkbox"]:checked::after {
  content: '✓' !important;
  position: absolute !important;
  top: 50% !important;
  left: 50% !important;
  transform: translate(-50%, -50%) !important;
  color: var(--text) !important;
  font-size: 10px !important;
  font-weight: bold !important;
}

.option-label {
  color: var(--text) !important;
  font-size: 0.8rem !important;
}
</style>
