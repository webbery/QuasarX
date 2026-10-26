<template>
  <aside class="ml-inspector">
    <div class="inspector-header">
      <h3>检查器</h3>
    </div>
    <div class="inspector-body">
      <div class="section">
        <h4>运行历史</h4>
        <div v-if="runs.length === 0" class="empty">暂无运行记录</div>
        <div v-else class="run-list">
          <div v-for="(run, i) in runs" :key="i" class="run-item">
            <span class="run-time">{{ run.at }}</span>
            <span class="run-stage">{{ run.stageLabel }}</span>
            <span class="run-detail">{{ run.detail }}</span>
          </div>
        </div>
      </div>

      <div class="section">
        <h4>标的选择</h4>
        <div class="symbol-actions">
          <button class="btn-small" @click="$emit('selectAllSymbols')">全选</button>
          <button class="btn-small" @click="$emit('clearSymbols')">清空</button>
        </div>
        <div v-if="securitiesLoading" class="loading">加载中...</div>
        <div v-else-if="securities.length === 0" class="empty">无可用标的</div>
        <div v-else class="symbol-list">
          <label v-for="sec in securities" :key="sec.code" class="symbol-item">
            <input
              type="checkbox"
              :checked="checkedSymbols.has(sec.code)"
              @change="$emit('toggleSymbol', sec.code)"
            />
            <span>{{ sec.name || sec.code }}</span>
          </label>
        </div>
      </div>
    </div>
  </aside>
</template>

<script setup lang="ts">
import type { SessionRun } from '@/types/ml'

interface Security {
  code: string
  name: string
}

defineProps<{
  state: any
  runs: SessionRun[]
  securities: Security[]
  checkedSymbols: Set<string>
  securitiesLoading: boolean
  labelSymbolOptions: string[]
}>()

defineEmits<{
  toggleSymbol: [code: string]
  selectAllSymbols: []
  clearSymbols: []
  goto: [key: string]
}>()
</script>

<style scoped>
.ml-inspector {
  background: #0d1218;
  border-left: 1px solid rgba(74, 85, 104, 0.25);
  overflow-y: auto;
  display: flex;
  flex-direction: column;
}

.inspector-header {
  padding: 12px 16px;
  border-bottom: 1px solid rgba(74, 85, 104, 0.25);
}

.inspector-header h3 {
  margin: 0;
  font-size: 13px;
  color: #e2e8f0;
  font-weight: 600;
}

.inspector-body {
  flex: 1;
  overflow-y: auto;
  padding: 12px 16px;
}

.section {
  margin-bottom: 20px;
}

.section h4 {
  margin: 0 0 8px;
  font-size: 11px;
  color: #94a3b8;
  font-weight: 600;
  text-transform: uppercase;
  letter-spacing: 0.5px;
}

.empty {
  font-size: 11px;
  color: #64748b;
  padding: 8px 0;
}

.loading {
  font-size: 11px;
  color: #94a3b8;
  padding: 8px 0;
}

.run-list {
  display: flex;
  flex-direction: column;
  gap: 6px;
}

.run-item {
  display: flex;
  gap: 8px;
  font-size: 10px;
  padding: 4px 0;
  border-bottom: 1px solid rgba(74, 85, 104, 0.1);
}

.run-time {
  color: #64748b;
  font-family: 'SF Mono', 'Consolas', monospace;
}

.run-stage {
  color: #5b8ff9;
  font-weight: 600;
}

.run-detail {
  color: #94a3b8;
  font-family: 'SF Mono', 'Consolas', monospace;
}

.symbol-actions {
  display: flex;
  gap: 6px;
  margin-bottom: 8px;
}

.btn-small {
  padding: 3px 8px;
  font-size: 10px;
  background: rgba(91, 143, 249, 0.1);
  border: 1px solid rgba(91, 143, 249, 0.3);
  border-radius: 3px;
  color: #5b8ff9;
  cursor: pointer;
}

.btn-small:hover {
  background: rgba(91, 143, 249, 0.2);
}

.symbol-list {
  display: flex;
  flex-direction: column;
  gap: 4px;
  max-height: 300px;
  overflow-y: auto;
}

.symbol-item {
  display: flex;
  align-items: center;
  gap: 6px;
  font-size: 11px;
  color: #cbd5e1;
  cursor: pointer;
  padding: 2px 0;
}

.symbol-item:hover {
  color: #e2e8f0;
}

.symbol-item input[type="checkbox"] {
  margin: 0;
}
</style>
