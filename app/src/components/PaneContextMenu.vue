<template>
  <div
    v-if="visible"
    class="context-menu"
    :style="{ left: x + 'px', top: y + 'px' }"
    @click.stop
  >
    <div class="context-menu-item" @click="$emit('fit-view')">
      <i class="fas fa-expand"></i>
      适配视图
    </div>
    <div class="context-menu-item" @click="$emit('auto-layout')">
      <i class="fas fa-diagram-project"></i>
      水平展开布局
    </div>
    <div class="context-menu-divider"></div>
    <div class="context-menu-item" @click="$emit('clear-canvas')">
      <i class="fas fa-broom"></i>
      清空画布
    </div>
    <div class="context-menu-item danger" @click="$emit('clear-and-new')">
      <i class="fas fa-file-circle-plus"></i>
      清空并新建策略
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
  'fit-view',
  'auto-layout',
  'clear-canvas',
  'clear-and-new'
])
</script>

<style scoped>
.context-menu {
  position: fixed;
  background: var(--panel-bg);
  border: 1px solid var(--border);
  border-radius: 8px;
  box-shadow: 0 4px 12px rgba(0, 0, 0, 0.3);
  z-index: 10000;
  min-width: 180px;
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

.context-menu-item.danger:hover {
  background-color: #dc2626;
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