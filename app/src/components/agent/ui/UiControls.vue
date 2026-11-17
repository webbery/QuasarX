<template>
  <div class="ui-controls">
    <div v-for="ctrl in controls" :key="ctrl.key" class="ctrl" :class="'is-' + ctrl.kind">
      <span class="ctrl-label" :data-tooltip="ctrl.hint">{{ ctrl.label }}</span>

      <!-- 滑块 -->
      <input
        v-if="ctrl.kind === 'slider'"
        class="ctrl-slider"
        type="range"
        :min="ctrl.min ?? 0"
        :max="ctrl.max ?? 100"
        :step="ctrl.step ?? 1"
        :value="numericValue(ctrl)"
        @input="emitValue(ctrl, ($event.target as HTMLInputElement).valueAsNumber)"
      />
      <span v-if="ctrl.kind === 'slider'" class="ctrl-value">
        {{ formatNumeric(numericValue(ctrl)) }}{{ ctrl.suffix ?? '' }}
      </span>

      <!-- 下拉 -->
      <select
        v-else-if="ctrl.kind === 'select'"
        class="ctrl-select"
        :value="String(params[ctrl.key] ?? '')"
        @change="emitValue(ctrl, ($event.target as HTMLSelectElement).value)"
      >
        <option v-for="opt in ctrl.options ?? []" :key="opt.value" :value="opt.value">
          {{ opt.label }}
        </option>
      </select>

      <!-- 开关 -->
      <label v-else-if="ctrl.kind === 'toggle'" class="ctrl-toggle">
        <input
          type="checkbox"
          :checked="Boolean(params[ctrl.key])"
          @change="emitValue(ctrl, ($event.target as HTMLInputElement).checked)"
        />
        <span class="ctrl-toggle-text">{{ params[ctrl.key] ? '开' : '关' }}</span>
      </label>

      <!-- 数字 -->
      <input
        v-else
        class="ctrl-number"
        type="number"
        :min="ctrl.min"
        :max="ctrl.max"
        :step="ctrl.step ?? 1"
        :value="numericValue(ctrl)"
        @input="emitValue(ctrl, ($event.target as HTMLInputElement).valueAsNumber)"
      />
    </div>

    <button v-if="showReset" class="ctrl-reset" data-tooltip="恢复默认参数" @click="$emit('reset')">
      <i class="fas fa-rotate-left"></i>
      重置
    </button>
  </div>
</template>

<script setup lang="ts">
import { computed } from 'vue'
import type { UiControlDef } from '@/lib/charts/types'

const props = defineProps<{
  controls: UiControlDef[]
  params: Record<string, unknown>
  /** 当前值是否已偏离默认 */
  dirty?: boolean
}>()

const emit = defineEmits<{
  (e: 'change', payload: { key: string; value: unknown }): void
  (e: 'reset'): void
}>()

const showReset = computed(() => props.dirty === true)

function numericValue(ctrl: UiControlDef): number {
  const v = props.params[ctrl.key]
  return typeof v === 'number' ? v : (ctrl.default as number) ?? 0
}

function formatNumeric(v: number): string {
  if (!Number.isFinite(v)) return '0'
  return Number.isInteger(v) ? String(v) : v.toFixed(2)
}

function emitValue(ctrl: UiControlDef, value: unknown) {
  emit('change', { key: ctrl.key, value })
}
</script>

<style scoped>
.ui-controls {
  display: flex;
  flex-wrap: wrap;
  align-items: center;
  gap: 8px 16px;
  padding: 9px 13px;
  border-top: 1px solid rgba(74, 85, 104, 0.22);
  background: rgba(255, 255, 255, 0.015);
}

.ctrl {
  display: inline-flex;
  align-items: center;
  gap: 7px;
  font-size: 11px;
  color: #6f7b8a;
}

.ctrl-label {
  white-space: nowrap;
}

.ctrl-slider {
  width: 92px;
  height: 3px;
  -webkit-appearance: none;
  appearance: none;
  background: rgba(74, 85, 104, 0.55);
  border-radius: 2px;
  outline: none;
  cursor: pointer;
}

.ctrl-slider::-webkit-slider-thumb {
  -webkit-appearance: none;
  appearance: none;
  width: 11px;
  height: 11px;
  border-radius: 50%;
  background: #2962ff;
  cursor: pointer;
}

.ctrl-slider::-moz-range-thumb {
  width: 11px;
  height: 11px;
  border: none;
  border-radius: 50%;
  background: #2962ff;
  cursor: pointer;
}

.ctrl-value {
  min-width: 38px;
  font-variant-numeric: tabular-nums;
  color: #a0aec0;
}

.ctrl-select {
  background: rgba(0, 0, 0, 0.3);
  border: 1px solid var(--border, #2a3449);
  border-radius: 5px;
  color: #e0e0e0;
  padding: 3px 7px;
  font-size: 11px;
  outline: none;
  cursor: pointer;
  font-family: inherit;
  max-width: 150px;
}

.ctrl-select option {
  background: var(--panel-bg, #1a2236);
  color: #e0e0e0;
}

.ctrl-toggle {
  display: inline-flex;
  align-items: center;
  gap: 5px;
  cursor: pointer;
}

.ctrl-toggle input {
  cursor: pointer;
  accent-color: #2962ff;
}

.ctrl-toggle-text {
  color: #a0aec0;
}

.ctrl-number {
  width: 68px;
  background: rgba(0, 0, 0, 0.3);
  border: 1px solid var(--border, #2a3449);
  border-radius: 5px;
  color: #e0e0e0;
  padding: 3px 7px;
  font-size: 11px;
  outline: none;
  font-family: inherit;
  font-variant-numeric: tabular-nums;
}

.ctrl-reset {
  margin-left: auto;
  height: 22px;
  padding: 0 8px;
  font-size: 11px;
  border-radius: 5px;
  border: 1px solid var(--border, #2a3449);
  background: transparent;
  color: #a0aec0;
  cursor: pointer;
  display: inline-flex;
  align-items: center;
  gap: 5px;
  font-family: inherit;
}

.ctrl-reset:hover {
  color: #e0e0e0;
  border-color: #2962ff;
}
</style>
