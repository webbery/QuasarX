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
