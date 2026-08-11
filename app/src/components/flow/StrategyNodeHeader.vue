<!-- src/components/flow/StrategyNodeHeader.vue -->
<!-- 节点头部组件 -->

<template>
  <div class="node-header" :class="headerClass">
    <div class="node-icon-wrapper">
      <div class="node-icon" :class="iconClass">
        <i :class="icon"></i>
      </div>
      <span class="node-type-tooltip">{{ nodeType }}</span>
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
  nodeType: string
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
  padding: 6px 10px;
  border-bottom: 1px solid var(--border);
  border-radius: 8px 8px 0 0;
  margin: -2px -2px 8px -2px;
  padding: 6px 12px;
}

/* === 节点头部背景色（按 nodeType 区分） === */
:global(.header-type-input)      { background: linear-gradient(135deg, #1b5e20, #2e7d32); }
:global(.header-type-function)   { background: linear-gradient(135deg, #0d47a1, #1565c0); }
:global(.header-type-formula)    { background: linear-gradient(135deg, #4a148c, #6a1b9a); }
:global(.header-type-signal)     { background: linear-gradient(135deg, #e65100, #ef6c00); }
:global(.header-type-execution)  { background: linear-gradient(135deg, #b71c1c, #c62828); }
:global(.header-type-xgboost)    { background: linear-gradient(135deg, #880e4f, #ad1457); }
:global(.header-type-backtest)   { background: linear-gradient(135deg, #3e2723, #4e342e); }
:global(.header-type-breakout)   { background: linear-gradient(135deg, #bf360c, #d84315); }
:global(.header-type-emd)        { background: linear-gradient(135deg, #006064, #00838f); }
:global(.header-type-cusum)      { background: linear-gradient(135deg, #1a237e, #283593); }
:global(.header-type-hmm)        { background: linear-gradient(135deg, #311b92, #4527a0); }
:global(.header-type-portfolio)  { background: linear-gradient(135deg, #004d40, #00695c); }
:global(.header-type-protection) { background: linear-gradient(135deg, #8b0000, #a52a2a); }
:global(.header-type-spread)     { background: linear-gradient(135deg, #33691e, #558b2f); }
:global(.header-type-factor_combine) { background: linear-gradient(135deg, #455a64, #607d8b); }
:global(.header-type-resample)   { background: linear-gradient(135deg, #00695c, #00897b); }
:global(.header-type-debug)      { background: linear-gradient(135deg, #424242, #616161); }
:global(.header-type-test)       { background: linear-gradient(135deg, #546e7a, #78909c); }

/* === 图标 + tooltip === */
.node-icon-wrapper {
  position: relative;
  margin-right: 10px;
  flex-shrink: 0;
}

.node-icon {
  width: 24px;
  height: 24px;
  border-radius: 50%;
  display: flex;
  align-items: center;
  justify-content: center;
  color: white;
  font-size: 12px;
  background-color: var(--primary);
  cursor: default;
}

.node-type-tooltip {
  position: absolute;
  bottom: calc(100% + 8px);
  left: 50%;
  transform: translateX(-50%);
  background: #1f2937;
  color: #e0e0e0;
  padding: 4px 8px;
  border-radius: 4px;
  font-size: 11px;
  font-weight: 400;
  white-space: nowrap;
  pointer-events: none;
  opacity: 0;
  transition: opacity 0.15s;
  z-index: 100;
}

.node-type-tooltip::after {
  content: '';
  position: absolute;
  top: 100%;
  left: 50%;
  transform: translateX(-50%);
  border: 4px solid transparent;
  border-top-color: #1f2937;
}

.node-icon-wrapper:hover .node-type-tooltip {
  opacity: 1;
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
