<template>
  <div class="strategy-selector">
    <!-- 策略选择 -->
    <div class="strategy-select">
      <label>策略:</label>
      <select
        :value="selectedStrategyId"
        class="select-small"
        @change="$emit('update:selectedStrategyId', ($event.target as HTMLSelectElement).value)"
      >
        <option value="">请选择策略</option>
        <option v-for="opt in strategyOptions" :key="opt.id" :value="opt.id">{{ opt.name }}</option>
      </select>
    </div>

    <!-- 标的多选下拉框 -->
    <div v-if="availableSecurities.length > 0" class="symbol-multiselect-dropdown" ref="symbolDropdownRef">
      <label>标的:</label>
      <div class="multiselect-trigger" @click="symbolDropdownOpen = !symbolDropdownOpen">
        <span class="multiselect-display">
          {{ checkedSymbols.size > 0 ? `已选 ${checkedSymbols.size} 个` : '选择标的' }}
        </span>
        <span class="multiselect-arrow">▼</span>
      </div>
      <div v-if="symbolDropdownOpen" class="multiselect-dropdown-content">
        <label
          v-for="sec in availableSecurities"
          :key="sec.code"
          class="multiselect-checkbox"
        >
          <input
            type="checkbox"
            :checked="checkedSymbols.has(sec.code)"
            @change="$emit('toggle-symbol', sec.code)"
          />
          <span>{{ sec.name ? `${sec.code}(${sec.name})` : sec.code }}</span>
        </label>
      </div>
    </div>
    <div v-else-if="selectedStrategyId && !loading" class="pool-empty">
      该策略未找到行情输入节点
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, onMounted, onUnmounted } from 'vue'

interface StrategyOption {
  id: string
  name: string
}

interface Security {
  code: string
  name?: string
}

defineProps<{
  selectedStrategyId: string
  strategyOptions: StrategyOption[]
  availableSecurities: Security[]
  checkedSymbols: Set<string>
  loading: boolean
}>()

defineEmits<{
  'update:selectedStrategyId': [value: string]
  'toggle-symbol': [code: string]
}>()

// 标的下拉框状态
const symbolDropdownOpen = ref(false)
const symbolDropdownRef = ref<HTMLElement | null>(null)

// 点击外部关闭下拉框
function handleClickOutside(event: MouseEvent) {
  if (symbolDropdownRef.value && !symbolDropdownRef.value.contains(event.target as Node)) {
    symbolDropdownOpen.value = false
  }
}

onMounted(() => {
  document.addEventListener('click', handleClickOutside)
})

onUnmounted(() => {
  document.removeEventListener('click', handleClickOutside)
})
</script>

<style scoped>
.strategy-selector {
  display: flex;
  align-items: center;
  gap: 12px;
}

.strategy-select {
  display: flex;
  align-items: center;
  gap: 6px;
}

.strategy-select label {
  font-size: 12px;
  color: #999;
  white-space: nowrap;
}

/* 标的多选下拉框 */
.symbol-multiselect-dropdown {
  position: relative;
  display: flex;
  align-items: center;
  gap: 6px;
}

.symbol-multiselect-dropdown label {
  font-size: 12px;
  color: #999;
  white-space: nowrap;
}

.multiselect-trigger {
  display: flex;
  align-items: center;
  gap: 6px;
  padding: 4px 8px;
  background: rgba(26, 34, 54, 0.8);
  border: 1px solid rgba(74, 85, 104, 0.3);
  border-radius: 4px;
  cursor: pointer;
  min-width: 120px;
  transition: border-color 0.2s;
}

.multiselect-trigger:hover {
  border-color: rgba(41, 98, 255, 0.5);
}

.multiselect-display {
  font-size: 12px;
  color: #e0e0e0;
}

.multiselect-arrow {
  font-size: 8px;
  color: #999;
  transition: transform 0.2s;
}

.multiselect-dropdown-content {
  position: absolute;
  top: 100%;
  left: 0;
  margin-top: 4px;
  background: rgba(26, 34, 54, 0.95);
  border: 1px solid rgba(74, 85, 104, 0.3);
  border-radius: 4px;
  padding: 8px;
  max-height: 300px;
  overflow-y: auto;
  z-index: 1000;
  min-width: 200px;
  box-shadow: 0 4px 12px rgba(0, 0, 0, 0.3);
  scrollbar-width: thin;
  scrollbar-color: rgba(255, 255, 255, 0.1) transparent;
}

.multiselect-dropdown-content::-webkit-scrollbar {
  width: 6px;
}

.multiselect-dropdown-content::-webkit-scrollbar-track {
  background: transparent;
}

.multiselect-dropdown-content::-webkit-scrollbar-thumb {
  background: rgba(255, 255, 255, 0.1);
  border-radius: 3px;
}

.multiselect-dropdown-content::-webkit-scrollbar-thumb:hover {
  background: rgba(255, 255, 255, 0.2);
}

.multiselect-checkbox {
  display: flex;
  align-items: center;
  gap: 6px;
  padding: 4px 8px;
  font-size: 12px;
  color: #e0e0e0;
  cursor: pointer;
  border-radius: 3px;
  transition: background 0.15s;
}

.multiselect-checkbox:hover {
  background: rgba(41, 98, 255, 0.1);
}

.multiselect-checkbox input[type="checkbox"] {
  accent-color: #2962ff;
  margin: 0;
  cursor: pointer;
}

.multiselect-checkbox input[type="checkbox"]:checked + span {
  color: #2962ff;
  font-weight: 500;
}

.pool-empty {
  font-size: 12px;
  color: #666;
  font-style: italic;
}

.select-small {
  padding: 4px 8px;
  background: rgba(26, 34, 54, 0.8);
  border: 1px solid rgba(74, 85, 104, 0.3);
  border-radius: 4px;
  color: #e0e0e0;
  font-size: 12px;
  outline: none;
  cursor: pointer;
}

.select-small:focus {
  border-color: rgba(41, 98, 255, 0.5);
}

.select-small option {
  background: #1a2236;
  color: #e0e0e0;
}
</style>
