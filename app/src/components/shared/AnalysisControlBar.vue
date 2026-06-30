<template>
  <div class="analysis-control-bar">
    <!-- 模式切换 -->
    <div class="mode-toggle">
      <button
        v-for="m in resolvedModes"
        :key="m.value"
        :class="['mode-btn', { active: mode === m.value }]"
        @click="$emit('update:mode', m.value)"
      >{{ m.label }}</button>
    </div>

    <!-- 策略模式：策略选择 + 标的选择 -->
    <template v-if="mode === 'strategy'">
      <div class="strategy-selector">
        <label>策略:</label>
        <select
          :value="resolvedSelectedStrategyId"
          class="select-small"
          @change="$emit('update:selectedStrategyId', ($event.target as HTMLSelectElement).value)"
        >
          <option value="">请选择策略</option>
          <option v-for="opt in resolvedStrategyOptions" :key="opt.id" :value="opt.id">{{ opt.name }}</option>
        </select>
      </div>

      <!-- 标的多选下拉框 -->
      <div v-if="resolvedAvailableSecurities.length > 0" class="symbol-multiselect-dropdown" ref="symbolDropdownRef">
        <label>标的:</label>
        <div class="multiselect-trigger" @click="symbolDropdownOpen = !symbolDropdownOpen">
          <span class="multiselect-display">
            {{ checkedSymbols.size > 0 ? `已选 ${checkedSymbols.size} 个` : '选择标的' }}
          </span>
          <span class="multiselect-arrow">▼</span>
        </div>
        <div v-if="symbolDropdownOpen" class="multiselect-dropdown-content">
          <label
            v-for="sec in resolvedAvailableSecurities"
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
      <div v-else-if="resolvedSelectedStrategyId && !loading" class="pool-empty">
        该策略未找到行情输入节点
      </div>
    </template>

    <!-- 宏观模式：国家 + 指标级联选择 -->
    <template v-else-if="mode === 'macro'">
      <div class="macro-selector">
        <label>国家:</label>
        <select
          :value="resolvedSelectedMacroCountry"
          class="select-small"
          @change="$emit('update:selectedMacroCountry', ($event.target as HTMLSelectElement).value)"
        >
          <option value="china">中国</option>
          <option value="usa">美国</option>
          <option value="global">全球</option>
        </select>
      </div>
      <div class="macro-selector">
        <label>指标:</label>
        <select
          :value="resolvedSelectedMacroIndicator"
          class="select-small"
        >
          <option value="">请选择指标</option>
          <option v-for="opt in resolvedFilteredMacroOptions" :key="opt.indicator" :value="opt.indicator">
            {{ opt.label }}
          </option>
        </select>
      </div>
    </template>

    <div class="control-spacer" />

    <!-- 插槽：各 Tab 自定义的额外控制项 -->
    <slot name="extra-controls" />

    <!-- 时间范围 -->
    <div class="time-selector">
      <select
        :value="quickRange"
        class="select-small"
        @change="$emit('update:quickRange', ($event.target as HTMLSelectElement).value)"
      >
        <option v-for="[label] in resolvedQuickRanges" :key="label" :value="label">{{ label }}</option>
      </select>
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
    </div>

    <!-- 频率选项（天、月、小时等） -->
    <div class="frequency-selector" v-if="resolvedShowFrequency">
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

    <!-- 开始分析按钮 -->
    <button class="btn btn-primary btn-small" :disabled="loading || !canAnalyze" @click="$emit('run-analysis')">
      {{ loading ? '分析中...' : '开始分析' }}
    </button>
  </div>
</template>

<script setup lang="ts">
import { ref, computed, onMounted, onUnmounted } from 'vue'

interface StrategyOption {
  id: string
  name: string
}

interface MacroOption {
  indicator: string
  label: string
}

interface Security {
  code: string
  name?: string
}

interface FrequencyOption {
  value: string
  label: string
}

interface Mode {
  value: string
  label: string
}

const props = defineProps<{
  mode: string
  modes?: Mode[]
  selectedStrategyId?: string
  strategyOptions?: StrategyOption[]
  availableSecurities?: Security[]
  checkedSymbols: Set<string>
  selectedMacroCountry?: string
  selectedMacroIndicator?: string
  filteredMacroOptions?: MacroOption[]
  quickRange: string
  quickRanges?: [string, () => [string, string]][]
  dateRange?: [string, string] | null
  frequency?: string
  showFrequency?: boolean
  frequencyOptions?: FrequencyOption[]
  loading: boolean
  canAnalyze: boolean
}>()

const emit = defineEmits<{
  'update:mode': [value: string]
  'update:selectedStrategyId': [value: string]
  'update:selectedMacroCountry': [value: string]
  'update:quickRange': [value: string]
  'update:frequency': [value: string]
  'update-date-range': [value: string, type: 'start' | 'end']
  'toggle-symbol': [code: string]
  'run-analysis': []
}>()

// 默认值（必须在 defineProps 之后声明，供 computed 使用）
const defaultModes: Mode[] = [
  { value: 'strategy', label: '策略行情' },
  { value: 'macro', label: '宏观指标' }
]

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

const resolvedModes = computed(() => props.modes ?? defaultModes)
const resolvedQuickRanges = computed(() => props.quickRanges ?? defaultQuickRanges)
const resolvedFrequencyOptions = computed(() => props.frequencyOptions ?? defaultFrequencyOptions)
const resolvedShowFrequency = computed(() => props.showFrequency ?? true)
const resolvedFrequency = computed(() => props.frequency ?? '1d')
const resolvedSelectedStrategyId = computed(() => props.selectedStrategyId ?? '')
const resolvedSelectedMacroCountry = computed(() => props.selectedMacroCountry ?? 'china')
const resolvedSelectedMacroIndicator = computed(() => props.selectedMacroIndicator ?? '')
const resolvedDateRange = computed(() => props.dateRange ?? null)
const resolvedStrategyOptions = computed(() => props.strategyOptions ?? [])
const resolvedAvailableSecurities = computed(() => props.availableSecurities ?? [])
const resolvedFilteredMacroOptions = computed(() => props.filteredMacroOptions ?? [])

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
.analysis-control-bar {
  display: flex;
  align-items: center;
  gap: 12px;
  padding: 10px 16px;
  background: rgba(26, 34, 54, 0.8);
  border-bottom: 1px solid rgba(74, 85, 104, 0.3);
  flex-wrap: wrap;
  min-height: 52px;
}

/* 模式切换按钮 */
.mode-toggle {
  display: flex;
  border: 1px solid rgba(74, 85, 104, 0.3);
  border-radius: 4px;
  overflow: hidden;
}

.mode-btn {
  padding: 4px 12px;
  background: rgba(26, 34, 54, 0.8);
  border: none;
  border-right: 1px solid rgba(74, 85, 104, 0.3);
  color: #999;
  font-size: 12px;
  cursor: pointer;
  transition: all 0.2s;
}

.mode-btn:last-child {
  border-right: none;
}

.mode-btn:hover {
  color: #e0e0e0;
}

.mode-btn.active {
  background: #2962ff;
  color: #fff;
}

.strategy-selector {
  display: flex;
  align-items: center;
  gap: 6px;
}

.strategy-selector label {
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

/* 宏观指标选择器 */
.macro-selector {
  display: flex;
  align-items: center;
  gap: 6px;
}

.macro-selector label {
  font-size: 12px;
  color: #999;
  white-space: nowrap;
}

.pool-empty {
  font-size: 12px;
  color: #666;
  font-style: italic;
}

.control-spacer {
  flex: 1;
  min-width: 8px;
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

.time-selector {
  display: flex;
  align-items: center;
  gap: 8px;
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

/* 按钮样式 */
.btn {
  padding: 6px 16px;
  border: 1px solid rgba(74, 85, 104, 0.3);
  border-radius: 4px;
  background: rgba(26, 34, 54, 0.8);
  color: #e0e0e0;
  font-size: 14px;
  cursor: pointer;
  transition: all 0.2s;
}

.btn:hover:not(:disabled) {
  border-color: rgba(41, 98, 255, 0.5);
}

.btn:disabled {
  opacity: 0.5;
  cursor: not-allowed;
}

.btn-primary {
  background: #2962ff;
  border-color: #2962ff;
  font-weight: 600;
}

.btn-primary:hover:not(:disabled) {
  background: #1e54e6;
  border-color: #1e54e6;
}

.btn-small {
  padding: 4px 12px;
  font-size: 12px;
}
</style>
