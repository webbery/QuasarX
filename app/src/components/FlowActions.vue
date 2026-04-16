<template>
  <div class="flow-actions">
    <button class="action-btn" @click="$emit('save')" title="保存策略图">
      <i class="fas fa-save"></i>
      保存
    </button>
    <button class="action-btn" @click="$emit('save-as')" title="另存为新版本">
      <i class="fas fa-save"></i>
      另存为
    </button>
    <button class="action-btn" @click="$emit('show-history')" title="策略面板">
      <i class="fas fa-redo"></i>
      策略面板
    </button>
    <button class="action-btn" @click="$emit('show-nodes')" title="节点面板">
      <i class="fas fa-play"></i>
      节点面板
    </button>
  </div>
</template>

<script setup>
defineEmits([
  'save',
  'save-as',
  'show-history',
  'show-nodes'
])
</script>

<style scoped>
/* 流程图操作按钮容器 */
.flow-actions {
  position: absolute;
  bottom: 20px;
  right: 20px;
  display: flex;
  flex-direction: column;
  gap: 10px;
  z-index: 1000;
  pointer-events: auto;
}

/* 功能按钮样式 */
.action-btn {
  display: flex;
  align-items: center;
  justify-content: flex-start;
  gap: 8px;
  padding: 10px 16px;
  background: rgba(30, 33, 45, 0.85);
  color: var(--text);
  border: 1px solid rgba(255, 255, 255, 0.1);
  border-radius: 8px;
  font-size: 14px;
  cursor: pointer;
  transition: all 0.3s ease;
  width: 110px;
  box-shadow: 0 4px 12px rgba(0, 0, 0, 0.25);
  opacity: 0.9;
}

.action-btn:hover {
  background: var(--primary);
  color: white;
  border-color: var(--primary);
  transform: translateY(-2px);
  box-shadow: 0 4px 12px rgba(41, 98, 255, 0.3);
  opacity: 1;
}

.action-btn:active {
  transform: translateY(0);
}

.action-btn i {
  font-size: 12px;
}
</style>
