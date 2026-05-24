<template>
  <div v-if="visible" class="dialog-overlay" @click.self="close">
    <div class="dialog-content">
      <h3>编辑标签</h3>
      <p class="dialog-subtitle">最多 5 个标签，用逗号分隔</p>

      <input
        v-model="tagsText"
        class="tags-input"
        placeholder="例如：财务, 年报, 分析"
        @keyup.enter="save"
      />

      <div class="tag-preview">
        <span v-for="tag in parsedTags" :key="tag" class="tag-chip-preview">{{ tag }}</span>
      </div>

      <p class="tag-count" :class="{ 'count-error': parsedTags.length > 5 }">
        当前 {{ parsedTags.length }}/5 个标签
      </p>

      <div class="dialog-actions">
        <button class="btn-cancel" @click="close">取消</button>
        <button class="btn-save" @click="save" :disabled="parsedTags.length > 5">保存</button>
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, computed, watch } from 'vue';
import type { KnowledgeDocument } from '../../stores/knowledgeStore';

const props = defineProps<{
  visible: boolean;
  document: KnowledgeDocument | null;
}>();

const emit = defineEmits<{
  'update:visible': [value: boolean];
  save: [doc: KnowledgeDocument, tags: string[]];
}>();

const tagsText = ref('');

watch(() => props.document, (doc) => {
  if (doc) {
    tagsText.value = (doc.tags || []).join(', ');
  } else {
    tagsText.value = '';
  }
}, { immediate: true });

const parsedTags = computed(() => {
  return tagsText.value
    .split(/[,，、]/)
    .map(t => t.trim())
    .filter(t => t.length > 0);
});

function close() {
  emit('update:visible', false);
}

function save() {
  if (!props.document) return;
  const tags = parsedTags.value.slice(0, 5);
  emit('save', props.document, tags);
  close();
}
</script>

<style scoped>
.dialog-overlay {
  position: fixed;
  top: 0;
  left: 0;
  width: 100%;
  height: 100%;
  background: rgba(0, 0, 0, 0.6);
  display: flex;
  align-items: center;
  justify-content: center;
  z-index: 1000;
}

.dialog-content {
  background: #1a1a2e;
  border: 1px solid rgba(255, 255, 255, 0.1);
  border-radius: 12px;
  padding: 24px;
  width: 400px;
  max-width: 90vw;
  box-shadow: 0 8px 32px rgba(0, 0, 0, 0.3);
}

.dialog-content h3 {
  margin: 0 0 4px;
  font-size: 16px;
  color: #f0f0f0;
}

.dialog-subtitle {
  margin: 0 0 16px;
  font-size: 12px;
  color: #8080a0;
}

.tags-input {
  width: 100%;
  padding: 10px 12px;
  border: 1px solid rgba(255, 255, 255, 0.15);
  border-radius: 8px;
  background: rgba(255, 255, 255, 0.05);
  color: #e0e0e0;
  font-size: 14px;
  outline: none;
  box-sizing: border-box;
  transition: border-color 0.2s;
}

.tags-input:focus {
  border-color: #60a5fa;
}

.tags-input::placeholder {
  color: #606080;
}

.tag-preview {
  display: flex;
  flex-wrap: wrap;
  gap: 6px;
  margin-top: 12px;
  min-height: 24px;
}

.tag-chip-preview {
  display: inline-block;
  padding: 3px 10px;
  border-radius: 10px;
  font-size: 12px;
  background: rgba(96, 165, 250, 0.15);
  color: #60a5fa;
}

.tag-count {
  margin-top: 8px;
  font-size: 12px;
  color: #8080a0;
}

.tag-count.count-error {
  color: #ef4444;
}

.dialog-actions {
  display: flex;
  justify-content: flex-end;
  gap: 10px;
  margin-top: 20px;
}

.btn-cancel {
  background: rgba(255, 255, 255, 0.08);
  color: #a0a0c0;
  border: 1px solid rgba(255, 255, 255, 0.1);
  padding: 8px 16px;
  border-radius: 8px;
  cursor: pointer;
  font-size: 13px;
  transition: all 0.2s;
}

.btn-cancel:hover {
  background: rgba(255, 255, 255, 0.12);
  color: #f0f0f0;
}

.btn-save {
  background: linear-gradient(90deg, #2563eb, #1d4ed8);
  color: white;
  border: none;
  padding: 8px 16px;
  border-radius: 8px;
  cursor: pointer;
  font-size: 13px;
  font-weight: 500;
  transition: all 0.2s;
}

.btn-save:hover:not(:disabled) {
  background: linear-gradient(90deg, #1d4ed8, #1e40af);
  transform: translateY(-1px);
}

.btn-save:disabled {
  opacity: 0.4;
  cursor: not-allowed;
}
</style>
