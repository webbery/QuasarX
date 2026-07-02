<template>
  <div class="date-range-selector">
    <!-- 快速范围选择 -->
    <select
      :value="quickRange"
      class="select-small"
      @change="$emit('update:quickRange', ($event.target as HTMLSelectElement).value)"
    >
      <option v-for="[label] in resolvedQuickRanges" :key="label" :value="label">{{ label }}</option>
    </select>

    <!-- 起止日期 -->
    <div class="date-range-inline">
      <input
        type="date"
        :value="resolvedDateRange?.[0]"
        @input="$emit('update-date-range', ($event.target as HTMLInputElement).value, 'start')"
        class="date-input-small"
        placeholder="开始"
      />
      <span class="date-sep">至</span>
      <input
        type="date"
        :value="resolvedDateRange?.[1]"
        @input="$emit('update-date-range', ($event.target as HTMLInputElement).value, 'end')"
        class="date-input-small"
        placeholder="结束"
      />
    </div>

    <!-- 频率选项 -->
    <div class="frequency-selector" v-if="showFrequency">
      <label>频率:</label>
      <select
        :value="resolvedFrequency"
        class="select-small"
        @change="$emit('update:frequency', ($event.target as HTMLSelectElement).value)"
      >
        <option v-for="f in resolvedFrequencyOptions" :key="f.value" :value="f.value">
          {{ f.label }}
        </option>
      </select>
    </div>
  </div>
</template>

<script setup lang="ts">
import { computed } from 'vue'

interface FrequencyOption {
  value: string
  label: string
}

const props = defineProps<{
  quickRange: string
  quickRanges?: [string, () => [string, string]][]
  dateRange?: [string, string] | null
  frequency?: string
  showFrequency?: boolean
  frequencyOptions?: FrequencyOption[]
}>()

const emit = defineEmits<{
  'update:quickRange': [value: string]
  'update:frequency': [value: string]
  'update-date-range': [value: string, type: 'start' | 'end']
}>()

// 默认值
const defaultQuickRanges: [string, () => [string, string]][] = [
  ['近1月', () => {
    const end = new Date()
    const start = new Date()
    start.setMonth(start.getMonth() - 1)
    return [start.toISOString().slice(0, 10), end.toISOString().slice(0, 10)]
  }],
  ['近3月', () => {
    const end = new Date()
    const start = new Date()
    start.setMonth(start.getMonth() - 3)
    return [start.toISOString().slice(0, 10), end.toISOString().slice(0, 10)]
  }],
  ['近6月', () => {
    const end = new Date()
    const start = new Date()
    start.setMonth(start.getMonth() - 6)
    return [start.toISOString().slice(0, 10), end.toISOString().slice(0, 10)]
  }],
  ['近1年', () => {
    const end = new Date()
    const start = new Date()
    start.setFullYear(start.getFullYear() - 1)
    return [start.toISOString().slice(0, 10), end.toISOString().slice(0, 10)]
  }],
  ['近3年', () => {
    const end = new Date()
    const start = new Date()
    start.setFullYear(start.getFullYear() - 3)
    return [start.toISOString().slice(0, 10), end.toISOString().slice(0, 10)]
  }],
]

const defaultFrequencyOptions: FrequencyOption[] = [
  { value: '1m', label: '1分钟' },
  { value: '5m', label: '5分钟' },
  { value: '15m', label: '15分钟' },
  { value: '30m', label: '30分钟' },
  { value: '1h', label: '1小时' },
  { value: '4h', label: '4小时' },
  { value: '1d', label: '日线' },
  { value: '1w', label: '周线' },
  { value: '1M', label: '月线' },
]

const resolvedQuickRanges = computed(() => props.quickRanges ?? defaultQuickRanges)
const resolvedFrequencyOptions = computed(() => props.frequencyOptions ?? defaultFrequencyOptions)
const resolvedFrequency = computed(() => props.frequency ?? '1d')
const resolvedDateRange = computed(() => props.dateRange ?? null)
</script>

<style scoped>
.date-range-selector {
  display: flex;
  align-items: center;
  gap: 8px;
}

/* 频率选择器 */
.frequency-selector {
  display: flex;
  align-items: center;
  gap: 6px;
}

.frequency-selector label {
  font-size: 12px;
  color: #999;
  white-space: nowrap;
}

.date-range-inline {
  display: flex;
  align-items: center;
  gap: 6px;
}

.date-input-small {
  padding: 4px 8px;
  background: rgba(26, 34, 54, 0.8);
  border: 1px solid rgba(74, 85, 104, 0.3);
  border-radius: 4px;
  color: #e0e0e0;
  font-size: 12px;
  outline: none;
  width: 120px;
}

.date-input-small:focus {
  border-color: rgba(41, 98, 255, 0.5);
}

.date-input-small::-webkit-calendar-picker-indicator {
  filter: invert(1);
  cursor: pointer;
}

.date-sep {
  color: #999;
  font-size: 12px;
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
