<!-- src/components/flow/StrategyNodeHeader.vue -->
<!-- 节点头部组件 -->

<template>
  <div class="node-header" :class="headerClass">
    <div class="node-icon" :class="iconClass">
      <i :class="icon"></i>
    </div>
    <div class="node-title" @dblclick="$emit('startEditing')">
      <span v-if="!isEditing">{{ label }}</span>
      <input
        v-else
        ref="titleInput"
        :value="editingLabel"
        @input="$emit('update:editingLabel', ($event.target as HTMLInputElement).value)"
        @blur="$emit('saveEditing')"
        @keyup.enter="$emit('saveEditing')"
        @keyup.esc="$emit('cancelEditing')"
        @keydown="$emit('inputKeydown', $event)"
        class="title-input"
        type="text"
        @mousedown.stop
        @dragstart.stop
      />
    </div>
    <div v-if="isMultiSelected" class="selection-badge">
      {{ selectionIndex }}
    </div>
  </div>
</template>

<script setup lang="ts">
defineProps<{
  label: string
  icon: string
  headerClass?: string
  iconClass?: string
  isEditing: boolean
  editingLabel: string
  isMultiSelected: boolean
  selectionIndex: number
}>()

defineEmits<{
  startEditing: []
  saveEditing: []
  cancelEditing: []
  inputKeydown: [event: KeyboardEvent]
  'update:editingLabel': [value: string]
}>()
</script>

<style scoped>
.node-header {
  display: flex;
  align-items: center;
  margin-bottom: 8px;
  padding-bottom: 8px;
  border-bottom: 1px solid var(--border);
}

.node-icon {
  width: 24px;
  height: 24px;
  border-radius: 50%;
  display: flex;
  align-items: center;
  justify-content: center;
  margin-right: 10px;
  color: white;
  font-size: 12px;
  background-color: var(--primary);
  flex-shrink: 0;
}

.node-title {
  font-weight: 600;
  color: var(--text);
  flex: 1;
  min-width: 0;
}

.title-input {
  width: 100%;
  background: transparent;
  border: 1px solid var(--border);
  border-radius: 4px;
  padding: 2px 6px;
  color: var(--text);
  font-size: inherit;
  font-weight: inherit;
  outline: none;
}

.title-input:focus {
  border-color: var(--primary);
}

.selection-badge {
  background: var(--accent);
  color: white;
  border-radius: 50%;
  width: 20px;
  height: 20px;
  display: flex;
  align-items: center;
  justify-content: center;
  font-size: 0.7rem;
  margin-left: 6px;
  flex-shrink: 0;
}
</style>
