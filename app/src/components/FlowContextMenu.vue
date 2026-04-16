<template>
  <div
    v-if="visible"
    class="context-menu"
    :style="{ left: x + 'px', top: y + 'px' }"
    @click.stop
  >
    <div class="context-menu-item" @click="$emit('delete-nodes')">
      <i class="fas fa-trash"></i>
      删除节点
    </div>
    <div class="context-menu-item" @click="$emit('duplicate-nodes')">
      <i class="fas fa-copy"></i>
      复制节点
    </div>
    <div class="context-menu-divider"></div>
    <div class="context-menu-item" @click="$emit('clear-selection')">
      <i class="fas fa-times"></i>
      取消选择
    </div>
  </div>
</template>

<script setup>
defineProps({
  visible: {
    type: Boolean,
    default: false
  },
  x: {
    type: Number,
    default: 0
  },
  y: {
    type: Number,
    default: 0
  }
})

defineEmits([
  'delete-nodes',
  'duplicate-nodes',
  'clear-selection'
])
</script>

<style scoped>
/* 添加上下文菜单样式 */
.context-menu {
  position: fixed;
  background: var(--panel-bg);
  border: 1px solid var(--border);
  border-radius: 8px;
  box-shadow: 0 4px 12px rgba(0, 0, 0, 0.3);
  z-index: 10000;
  min-width: 150px;
  padding: 8px 0;
  backdrop-filter: blur(10px);
}

.context-menu-item {
  padding: 8px 16px;
  cursor: pointer;
  display: flex;
  align-items: center;
  gap: 8px;
  font-size: 14px;
  color: var(--text);
  transition: background-color 0.2s ease;
}

.context-menu-item:hover {
  background-color: var(--primary);
  color: white;
}

.context-menu-item i {
  width: 16px;
  text-align: center;
}

.context-menu-divider {
  height: 1px;
  background-color: var(--border);
  margin: 4px 0;
}
</style>
