<template>
  <div class="edge-imf-selector">
    <div class="selector-header">
      <i class="fas fa-project-diagram"></i>
      <span>IMF 分量路由</span>
      <span class="target-label">→ {{ targetNodeLabel }}</span>
    </div>
    <div class="selector-row">
      <label class="selector-label">输出分量</label>
      <select class="imf-select" :value="selectValue" @change="onChange">
        <option value="">未选择</option>
        <option v-for="i in numImfs" :key="i - 1" :value="i - 1">IMF_{{ i - 1 }}</option>
      </select>
    </div>
    <div class="selector-hint">
      <template v-if="currentImf === null">所有 IMF 分量均输出到目标节点</template>
      <template v-else>仅 IMF_{{ currentImf }} 输出到目标节点</template>
    </div>
  </div>
</template>

<script setup>
import { computed } from 'vue'

const props = defineProps({
  edge: { type: Object, required: true },
  numImfs: { type: Number, default: 5 },
  targetNodeLabel: { type: String, default: '' },
})

const emit = defineEmits(['set'])

const currentImf = computed(() => {
  const idx = props.edge?.data?.imfIndex
  return (idx === undefined || idx === null) ? null : Number(idx)
})

const selectValue = computed(() => currentImf.value === null ? '' : currentImf.value)

function onChange(e) {
  const raw = e.target.value
  const index = raw === '' ? null : Number(raw)
  if (currentImf.value === index) return
  emit('set', index)
}
</script>

<style scoped>
.edge-imf-selector {
  margin-top: 8px;
  padding: 8px 10px;
  border-top: 1px solid var(--border);
  background: rgba(41, 98, 255, 0.04);
}

.selector-header {
  display: flex;
  align-items: center;
  gap: 6px;
  font-size: 0.78rem;
  font-weight: 600;
  color: var(--text);
  margin-bottom: 8px;
}

.selector-header i {
  color: var(--primary);
  font-size: 0.75rem;
}

.target-label {
  margin-left: auto;
  font-weight: 400;
  font-size: 0.72rem;
  color: var(--text-secondary);
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
  max-width: 120px;
}

.selector-row {
  display: flex;
  align-items: center;
  gap: 8px;
}

.selector-label {
  font-size: 0.72rem;
  color: var(--text-secondary);
  white-space: nowrap;
  flex-shrink: 0;
}

.imf-select {
  flex: 1;
  min-width: 0;
  padding: 4px 8px;
  font-size: 0.75rem;
  border: 1px solid var(--border);
  border-radius: 4px;
  background: var(--panel-bg);
  color: var(--text);
  outline: none;
  cursor: pointer;
  transition: border-color 0.15s ease;
}

.imf-select:hover,
.imf-select:focus {
  border-color: var(--primary);
}

.selector-hint {
  margin-top: 6px;
  font-size: 0.68rem;
  color: var(--text-secondary);
  line-height: 1.4;
}
</style>
