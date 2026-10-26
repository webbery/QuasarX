<template>
  <div class="set-bar">
    <div class="sb-group">
      <span class="sb-label">配方集</span>
      <select class="sb-select" :value="current.id" @change="onSelect">
        <option v-for="s in sets" :key="s.id" :value="s.id">{{ s.name }}</option>
        <option v-if="!sets.some(s => s.id === current.id)" :value="current.id">{{ current.name }}（未保存）</option>
      </select>
      <input class="sb-name" :value="current.name" @input="patch({ name: ($event.target as HTMLInputElement).value })"
             placeholder="配方集名称" />
      <span v-if="dirty" class="sb-dirty" title="有未保存的修改">●</span>
    </div>

    <div class="sb-group">
      <button class="sb-btn" :disabled="collecting" @click="$emit('save')">保存</button>
      <button class="sb-btn ghost" :disabled="collecting" @click="$emit('save-as')">另存</button>
      <button class="sb-btn ghost" :disabled="collecting" @click="$emit('new')">新建</button>
    </div>

    <div class="sb-group">
      <span class="sb-label">区间</span>
      <input type="date" class="sb-date" :value="current.dateRange[0]"
             @change="patchDate(0, ($event.target as HTMLInputElement).value)" />
      <span class="sb-arrow">→</span>
      <input type="date" class="sb-date" :value="current.dateRange[1]"
             @change="patchDate(1, ($event.target as HTMLInputElement).value)" />
    </div>

    <div class="sb-group">
      <span class="sb-label">频率</span>
      <select class="sb-select narrow" :value="current.frequency"
              @change="patch({ frequency: ($event.target as HTMLSelectElement).value })">
        <option v-for="f in FREQ_OPTIONS" :key="f.value" :value="f.value">{{ f.label }}</option>
      </select>
    </div>

    <div class="sb-group right">
      <span v-if="issueCount" class="sb-issue" :title="issueText">{{ issueCount }} 个问题</span>
      <button class="sb-go" :disabled="collecting || issueCount > 0" @click="$emit('collect')">
        {{ collecting ? '收集中…' : '开始收集' }}
      </button>
    </div>
  </div>
</template>

<script setup lang="ts">
import type { RecipeSet } from './types'

const FREQ_OPTIONS = [
  { value: '1d', label: '日线' },
  { value: '1w', label: '周线' },
  { value: '1m', label: '1分钟' },
  { value: '5m', label: '5分钟' },
  { value: '15m', label: '15分钟' },
  { value: '30m', label: '30分钟' },
  { value: '1h', label: '1小时' },
]

const props = defineProps<{
  sets: RecipeSet[]
  current: RecipeSet
  dirty: boolean
  collecting: boolean
  issueCount: number
  issueText: string
}>()

const emit = defineEmits<{
  (e: 'select', id: string): void
  (e: 'patch', patch: Partial<RecipeSet>): void
  (e: 'save'): void
  (e: 'save-as'): void
  (e: 'new'): void
  (e: 'collect'): void
}>()

function patch(p: Partial<RecipeSet>) {
  emit('patch', p)
}

function patchDate(idx: 0 | 1, value: string) {
  const next: [string, string] = [props.current.dateRange[0], props.current.dateRange[1]]
  next[idx] = value
  emit('patch', { dateRange: next })
}

function onSelect(e: Event) {
  emit('select', (e.target as HTMLSelectElement).value)
}
</script>

<style scoped>
.set-bar {
  flex: 0 0 auto;
  display: flex;
  align-items: center;
  flex-wrap: wrap;
  gap: 8px 14px;
  padding: 7px 12px;
  background: #0b1015;
  border-bottom: 1px solid rgba(74, 85, 104, 0.25);
}

.sb-group { display: flex; align-items: center; gap: 6px; }
.sb-group.right { margin-left: auto; }

.sb-label { font-size: 9.5px; letter-spacing: 1.2px; color: #7d8c9e; font-weight: 600; }

.sb-select, .sb-name, .sb-date {
  padding: 4px 7px;
  background: #0f161e;
  border: 1px solid rgba(74, 85, 104, 0.4);
  border-radius: 3px;
  color: #dbe4ee;
  font-size: 11px;
  font-family: inherit;
  outline: none;
}
.sb-select { min-width: 150px; }
.sb-select.narrow { min-width: 82px; }
.sb-name { width: 150px; }
.sb-date {
  width: 116px;
  font-family: 'SF Mono', 'Cascadia Mono', 'Consolas', monospace;
  font-size: 10.5px;
}
.sb-select:focus, .sb-name:focus, .sb-date:focus { border-color: #38bdf8; }
.sb-date::-webkit-calendar-picker-indicator { filter: invert(0.7); cursor: pointer; }

.sb-arrow { color: #4f5d6d; font-size: 10px; }

.sb-dirty { color: #fbbf24; font-size: 10px; line-height: 1; }

.sb-btn {
  padding: 4px 11px;
  background: rgba(56, 189, 248, 0.13);
  border: 1px solid rgba(56, 189, 248, 0.45);
  border-radius: 3px;
  color: #a5e0ff;
  font-family: inherit;
  font-size: 10.5px;
  cursor: pointer;
}
.sb-btn.ghost { background: transparent; border-color: rgba(74, 85, 104, 0.4); color: #94a3b8; }
.sb-btn:disabled { opacity: 0.45; cursor: not-allowed; }
.sb-btn:hover:not(:disabled) { background: rgba(56, 189, 248, 0.2); }

.sb-issue { font-size: 10px; color: #fbbf24; cursor: help; }

.sb-go {
  padding: 5px 16px;
  background: rgba(56, 189, 248, 0.13);
  border: 1px solid rgba(56, 189, 248, 0.45);
  border-radius: 3px;
  color: #a5e0ff;
  font-family: inherit;
  font-size: 11.5px;
  cursor: pointer;
}
.sb-go:hover:not(:disabled) { background: rgba(56, 189, 248, 0.22); }
.sb-go:disabled { opacity: 0.45; cursor: not-allowed; }
</style>
