<template>
  <div class="right-panel-tabs">
    <button
      class="tab-btn"
      :class="{ active: modelValue === 'components' }"
      @click="$emit('update:modelValue', 'components')"
      title="节点面板"
    >
      <i class="fas fa-puzzle-piece"></i>
      <span>节点面板</span>
    </button>
    <button
      class="tab-btn"
      :class="{ active: modelValue === 'strategy' }"
      @click="$emit('update:modelValue', 'strategy')"
      title="策略面板"
    >
      <i class="fas fa-history"></i>
      <span>策略面板</span>
    </button>
  </div>
</template>

<script setup>
defineProps({
  modelValue: {
    type: String,
    required: true,
    validator: (value) => ['components', 'strategy'].includes(value)
  }
})

defineEmits(['update:modelValue'])
</script>

<style scoped>
.right-panel-tabs {
  display: flex;
  background-color: var(--darker-bg);
  border-bottom: 1px solid var(--border);
  padding: 0;
}

.tab-btn {
  flex: 1;
  padding: 12px 16px;
  background: none;
  border: none;
  font-size: 13px;
  cursor: pointer;
  color: var(--text-secondary);
  border-bottom: 2px solid transparent;
  transition: all 0.3s ease;
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 6px;
  user-select: none;
}

.tab-btn:hover {
  color: var(--text);
  background: rgba(41, 98, 255, 0.1);
}

.tab-btn.active {
  color: var(--primary);
  border-bottom-color: var(--primary);
  background: rgba(41, 98, 255, 0.15);
}

.tab-btn i {
  font-size: 14px;
}
</style>
