<template>
  <teleport to="body">
    <Transition name="fade">
      <div v-if="visible" class="dialog-overlay" @click.self="onClose">
        <div class="dialog-content">
          <!-- 标题 -->
          <div class="dialog-header">
            <h3 class="dialog-title">
              <i class="fas fa-file-pdf"></i>
              {{ document?.title || '文档详情' }}
            </h3>
            <button class="close-btn" @click="onClose">
              <i class="fas fa-times"></i>
            </button>
          </div>

          <div class="dialog-body">
            <!-- 基本信息 -->
            <div class="info-section">
              <h4 class="section-title">基本信息</h4>
              <div class="info-grid">
                <div class="info-item">
                  <span class="info-label">文件名</span>
                  <span class="info-value">{{ document?.fileName || '-' }}</span>
                </div>
                <div class="info-item">
                  <span class="info-label">上传时间</span>
                  <span class="info-value">{{ document ? formatFullTime(document.uploadTime) : '-' }}</span>
                </div>
                <div class="info-item">
                  <span class="info-label">页数</span>
                  <span class="info-value">{{ document?.pages || '-' }}</span>
                </div>
                <div class="info-item">
                  <span class="info-label">检索命中</span>
                  <span class="info-value">{{ document?.hitCount || 0 }} 次</span>
                </div>
                <div class="info-item">
                  <span class="info-label">状态</span>
                  <span class="info-value">
                    <span
                      class="status-badge"
                      :class="{
                        'status-parsing': document?.status === 'parsing',
                        'status-ready': document?.status === 'ready',
                        'status-error': document?.status === 'error',
                      }"
                    >
                      {{ statusText }}
                    </span>
                  </span>
                </div>
              </div>
            </div>

            <!-- 摘要 -->
            <div class="info-section">
              <h4 class="section-title">摘要</h4>
              <div class="summary-box">
                <p>{{ document?.summary || '暂无摘要' }}</p>
              </div>
            </div>

            <!-- 文本预览 -->
            <div class="info-section">
              <h4 class="section-title">
                文本预览
                <span class="chunk-count">({{ document?.chunks?.length || 0 }} 个片段)</span>
              </h4>
              <div class="chunks-preview">
                <div
                  v-for="chunk in (document?.chunks || []).slice(0, 5)"
                  :key="chunk.index"
                  class="chunk-item"
                >
                  <div class="chunk-header">
                    <span class="chunk-badge">#{{ chunk.index + 1 }}</span>
                  </div>
                  <p class="chunk-text">{{ chunk.content }}</p>
                </div>
                <div v-if="(document?.chunks?.length || 0) > 5" class="more-hint">
                  还有 {{ (document?.chunks?.length || 0) - 5 }} 个片段...
                </div>
              </div>
            </div>
          </div>

          <!-- 底部操作 -->
          <div class="dialog-footer">
            <button class="footer-btn" @click="onClose">关闭</button>
          </div>
        </div>
      </div>
    </Transition>
  </teleport>
</template>

<script setup lang="ts">
import { computed } from 'vue';
import type { KnowledgeDocument } from '../../stores/knowledgeStore';

const props = defineProps<{
  visible: boolean;
  document: KnowledgeDocument | null;
}>();

const emit = defineEmits<{
  'update:visible': [value: boolean];
}>();

const statusText = computed(() => {
  switch (props.document?.status) {
    case 'parsing':
      return '解析中';
    case 'ready':
      return '已完成';
    case 'error':
      return '错误';
    default:
      return '-';
  }
});

function formatFullTime(timestamp: number): string {
  const date = new Date(timestamp);
  return `${date.getFullYear()}-${String(date.getMonth() + 1).padStart(2, '0')}-${String(date.getDate()).padStart(2, '0')} ${String(date.getHours()).padStart(2, '0')}:${String(date.getMinutes()).padStart(2, '0')}:${String(date.getSeconds()).padStart(2, '0')}`;
}

function onClose() {
  emit('update:visible', false);
}
</script>

<style scoped>
/* Overlay */
.dialog-overlay {
  position: fixed;
  top: 0;
  left: 0;
  width: 100vw;
  height: 100vh;
  background: rgba(0, 0, 0, 0.6);
  display: flex;
  align-items: center;
  justify-content: center;
  z-index: 9999;
}

/* Dialog */
.dialog-content {
  background: #1a1a2e;
  border-radius: 16px;
  width: 90%;
  max-width: 720px;
  max-height: 80vh;
  display: flex;
  flex-direction: column;
  box-shadow: 0 20px 60px rgba(0, 0, 0, 0.4);
  border: 1px solid rgba(255, 255, 255, 0.08);
}

/* Header */
.dialog-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 20px 24px;
  border-bottom: 1px solid rgba(255, 255, 255, 0.06);
}

.dialog-title {
  margin: 0;
  font-size: 18px;
  font-weight: 600;
  color: #f0f0f0;
  display: flex;
  align-items: center;
  gap: 10px;
}

.dialog-title i {
  color: #ef4444;
}

.close-btn {
  background: transparent;
  border: none;
  color: #a0a0c0;
  cursor: pointer;
  font-size: 16px;
  padding: 6px;
  border-radius: 6px;
  transition: all 0.2s;
}

.close-btn:hover {
  background: rgba(255, 255, 255, 0.08);
  color: #f0f0f0;
}

/* Body */
.dialog-body {
  flex: 1;
  overflow-y: auto;
  padding: 20px 24px;
}

/* Sections */
.info-section {
  margin-bottom: 24px;
}

.section-title {
  font-size: 14px;
  font-weight: 600;
  color: #60a5fa;
  margin: 0 0 12px 0;
  display: flex;
  align-items: center;
  gap: 8px;
}

.chunk-count {
  font-size: 12px;
  color: #a0a0c0;
  font-weight: 400;
}

/* Info grid */
.info-grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(200px, 1fr));
  gap: 12px;
}

.info-item {
  display: flex;
  flex-direction: column;
  gap: 4px;
}

.info-label {
  font-size: 12px;
  color: #606080;
}

.info-value {
  font-size: 14px;
  color: #e0e0e0;
}

/* Summary */
.summary-box {
  background: rgba(255, 255, 255, 0.04);
  border-radius: 8px;
  padding: 12px;
  font-size: 13px;
  line-height: 1.6;
  color: #c0c0d0;
}

.summary-box p {
  margin: 0;
}

/* Chunks */
.chunks-preview {
  display: flex;
  flex-direction: column;
  gap: 10px;
  max-height: 300px;
  overflow-y: auto;
}

.chunk-item {
  background: rgba(255, 255, 255, 0.03);
  border-radius: 8px;
  padding: 12px;
  border-left: 3px solid #60a5fa;
}

.chunk-header {
  margin-bottom: 6px;
}

.chunk-badge {
  display: inline-block;
  background: rgba(96, 165, 250, 0.15);
  color: #60a5fa;
  font-size: 11px;
  padding: 2px 8px;
  border-radius: 10px;
}

.chunk-text {
  margin: 0;
  font-size: 13px;
  line-height: 1.6;
  color: #c0c0d0;
  overflow: hidden;
  text-overflow: ellipsis;
  display: -webkit-box;
  -webkit-line-clamp: 3;
  -webkit-box-orient: vertical;
}

.more-hint {
  text-align: center;
  color: #606080;
  font-size: 13px;
  padding: 8px;
}

/* Status badge */
.status-badge {
  display: inline-block;
  padding: 3px 10px;
  border-radius: 12px;
  font-size: 12px;
  font-weight: 500;
}

.status-parsing {
  background: rgba(251, 191, 36, 0.15);
  color: #fbbf24;
}

.status-ready {
  background: rgba(74, 222, 128, 0.15);
  color: #4ade80;
}

.status-error {
  background: rgba(239, 68, 68, 0.15);
  color: #ef4444;
}

/* Footer */
.dialog-footer {
  display: flex;
  justify-content: center;
  padding: 16px 24px;
  border-top: 1px solid rgba(255, 255, 255, 0.06);
}

.footer-btn {
  background: linear-gradient(90deg, #2563eb, #1d4ed8);
  color: white;
  border: none;
  padding: 10px 24px;
  border-radius: 8px;
  font-size: 14px;
  font-weight: 500;
  cursor: pointer;
  transition: all 0.3s;
}

.footer-btn:hover {
  background: linear-gradient(90deg, #1d4ed8, #1e40af);
  transform: translateY(-1px);
}

/* Transition */
.fade-enter-active,
.fade-leave-active {
  transition: opacity 0.2s ease;
}

.fade-enter-from,
.fade-leave-to {
  opacity: 0;
}
</style>
