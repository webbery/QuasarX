<template>
  <div class="analysis-right-panel">
    <div class="panel-header">
      <span class="panel-title">分析面板配置</span>
    </div>
    <div class="panel-list">
      <div
        v-for="item in panels"
        :key="item.id"
        class="panel-item"
        @click="onPanelClick(item)"
      >
        <label class="panel-checkbox" @click.stop>
          <input type="checkbox" v-model="item.enabled" />
          <span class="checkmark"></span>
        </label>
        <span class="panel-label">{{ item.label }}</span>
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { useAnalysisPanelConfig } from '@/composables/useAnalysisPanelConfig'

const { panels, toggle } = useAnalysisPanelConfig()

function onPanelClick(item: { id: string; enabled: boolean }) {
  if (!item.enabled) {
    toggle(item.id)
  }
  window.dispatchEvent(new CustomEvent('switch-analysis-tab', { detail: { tab: item.id } }))
}
</script>

<style scoped>
.analysis-right-panel {
  display: flex;
  flex-direction: column;
  height: 100%;
  padding: 12px;
}

.panel-header {
  margin-bottom: 12px;
  padding-bottom: 8px;
  border-bottom: 1px solid var(--border, #333);
}

.panel-title {
  font-size: 13px;
  font-weight: 600;
  color: var(--text-primary, #e0e0e0);
}

.panel-list {
  display: flex;
  flex-direction: column;
  gap: 4px;
}

.panel-item {
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 8px 10px;
  border-radius: 4px;
  cursor: pointer;
  transition: background 0.15s;
}

.panel-item:hover {
  background: var(--hover-bg, rgba(255, 255, 255, 0.05));
}

.panel-checkbox {
  position: relative;
  display: flex;
  align-items: center;
  cursor: pointer;
}

.panel-checkbox input {
  width: 14px;
  height: 14px;
  accent-color: var(--primary, #4a9eff);
  cursor: pointer;
}

.panel-label {
  font-size: 13px;
  color: var(--text-secondary, #b0b0b0);
  user-select: none;
}

.panel-item:hover .panel-label {
  color: var(--text-primary, #e0e0e0);
}
</style>
