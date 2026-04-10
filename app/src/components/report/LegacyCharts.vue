<!-- app/src/components/report/LegacyCharts.vue -->
<!-- 向后兼容占位组件 - 当旧图表还未迁移时显示 -->

<template>
  <div class="legacy-charts">
    <div class="placeholder-message">
      <div class="icon">🚧</div>
      <div class="text">更多图表正在迁移中...</div>
      <div class="hint">请使用原 ReportView 查看完整图表</div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { defineExpose } from 'vue'

function initializeCharts() {
  console.info('[LegacyCharts] 占位图表组件，无需初始化')
}

defineExpose({
  initializeCharts
})
</script>

<style scoped>
.legacy-charts {
  display: flex;
  flex-direction: column;
  gap: 20px;
}

.placeholder-message {
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  padding: 60px 20px;
  background: var(--panel-bg, #1a2236);
  border-radius: 12px;
  border: 1px dashed var(--border, #2a3449);
  color: var(--text-secondary, #a0aec0);
}

.placeholder-message .icon {
  font-size: 48px;
  margin-bottom: 12px;
}

.placeholder-message .text {
  font-size: 16px;
  font-weight: 500;
  margin-bottom: 8px;
}

.placeholder-message .hint {
  font-size: 13px;
  opacity: 0.7;
}
</style>
