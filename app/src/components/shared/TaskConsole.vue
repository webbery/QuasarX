<template>
  <div class="task-console" :class="{ collapsed }">
    <div class="console-header" @click="$emit('toggle')">
      <span class="console-title">{{ title }}</span>
      <span v-if="taskLabel" class="console-task">{{ taskLabel }}</span>
      <span v-if="summary" class="console-summary">{{ summary }}</span>
      <span class="console-toggle">{{ collapsed ? '▲' : '▼' }}</span>
    </div>
    <div v-show="!collapsed" class="console-body">
      <div v-if="rows.length === 0" class="console-empty">暂无日志</div>
      <div v-else class="console-rows">
        <div v-for="(row, i) in rows" :key="i" class="console-row" :class="row.level">
          <span class="row-tag">{{ row.tag }}</span>
          <span class="row-text">{{ row.text }}</span>
        </div>
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import type { ConsoleRow } from '@/types/ml'

defineProps<{
  title: string
  rows: ConsoleRow[]
  taskLabel?: string
  summary?: string
  collapsed: boolean
}>()

defineEmits<{
  toggle: []
}>()
</script>

<style scoped>
.task-console {
  background: #0a0e13;
  border-top: 1px solid rgba(74, 85, 104, 0.25);
  display: flex;
  flex-direction: column;
  max-height: 200px;
}

.task-console.collapsed {
  max-height: 32px;
}

.console-header {
  display: flex;
  align-items: center;
  gap: 12px;
  padding: 6px 12px;
  background: rgba(15, 25, 41, 0.6);
  border-bottom: 1px solid rgba(74, 85, 104, 0.2);
  cursor: pointer;
  user-select: none;
}

.console-header:hover {
  background: rgba(15, 25, 41, 0.8);
}

.console-title {
  font-size: 11px;
  color: #94a3b8;
  font-weight: 600;
  text-transform: uppercase;
  letter-spacing: 0.5px;
}

.console-task {
  font-size: 11px;
  color: #5b8ff9;
  font-weight: 600;
}

.console-summary {
  font-size: 10px;
  color: #64748b;
  font-family: 'SF Mono', 'Consolas', monospace;
  margin-left: auto;
}

.console-toggle {
  font-size: 10px;
  color: #64748b;
}

.console-body {
  flex: 1;
  overflow-y: auto;
  padding: 8px 12px;
}

.console-empty {
  font-size: 11px;
  color: #64748b;
  padding: 8px 0;
}

.console-rows {
  display: flex;
  flex-direction: column;
  gap: 2px;
}

.console-row {
  display: flex;
  gap: 8px;
  font-size: 10px;
  padding: 2px 0;
  font-family: 'SF Mono', 'Consolas', monospace;
}

.row-tag {
  color: #64748b;
  min-width: 20px;
}

.row-text {
  color: #cbd5e1;
}

.console-row.error .row-tag {
  color: #ea3943;
}

.console-row.error .row-text {
  color: #ea3943;
}

.console-row.done .row-tag {
  color: #26a65b;
}

.console-row.warn .row-tag {
  color: #fbbf24;
}

.console-row.info .row-tag {
  color: #5b8ff9;
}
</style>
