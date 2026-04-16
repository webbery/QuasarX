<template>
  <div class="tab-header">
    <div class="tabs">
      <button
        class="tab-button"
        :class="{ active: modelValue === 'flow' }"
        @click="$emit('update:modelValue', 'flow')"
      >
        <i class="fas fa-project-diagram"></i>
        流程图
      </button>
      <button
        class="tab-button"
        :class="{ active: modelValue === 'backtest' }"
        @click="$emit('update:modelValue', 'backtest')"
      >
        <i class="fas fa-chart-line"></i>
        回测结果
      </button>
    </div>
  </div>
</template>

<script setup>
defineProps({
  modelValue: {
    type: String,
    required: true,
    validator: (value) => ['flow', 'backtest'].includes(value)
  }
})

defineEmits(['update:modelValue'])
</script>

<style scoped>
/* 头部容器样式 */
.tab-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  background-color: var(--darker-bg);
  border-bottom: 1px solid var(--border);
  padding: 0 16px;
}

/* 选项卡样式 */
.tabs {
  display: flex;
  background-color: var(--darker-bg);
  border-bottom: 1px solid var(--border);
  flex: 1;
}

.tab-button {
  padding: 12px 24px;
  background: none;
  border: none;
  font-size: 14px;
  cursor: pointer;
  color: var(--text-secondary);
  border-bottom: 2px solid transparent;
  transition: all 0.3s ease;
  display: flex;
  align-items: center;
  gap: 8px;
}

.tab-button:hover {
  color: var(--text);
  background: rgba(41, 98, 255, 0.1);
}

.tab-button.active {
  color: var(--primary);
  border-bottom-color: var(--primary);
  background: rgba(41, 98, 255, 0.15);
}
</style>
