<template>
  <Teleport to="body">
    <transition name="context-fade">
      <div
        v-if="visible"
        class="context-menu-overlay"
        @click="close"
        @contextmenu.prevent="close"
      >
        <div
          class="context-menu"
          :style="{ left: position.x + 'px', top: position.y + 'px' }"
          @click.stop
        >
          <div class="context-menu-header">
            <i class="fas fa-file-pdf pdf-icon"></i>
            <span class="file-name" :title="doc?.fileName">{{ doc?.title }}</span>
          </div>

          <div class="context-menu-body">
            <!-- 索引操作 -->
            <div class="menu-section-label">索引</div>

            <div
              v-if="doc?.summaryStatus === 'pending'"
              class="context-menu-item"
              @click="handleAction('generateIndex')"
            >
              <i class="fas fa-magic menu-icon icon-generate"></i>
              <span>生成索引</span>
              <span class="menu-shortcut">Ctrl+I</span>
            </div>

            <div
              v-if="doc?.summaryStatus === 'failed'"
              class="context-menu-item"
              @click="handleAction('retryIndex')"
            >
              <i class="fas fa-redo menu-icon icon-retry"></i>
              <span>重新生成索引</span>
              <span class="menu-shortcut">Ctrl+R</span>
            </div>

            <div
              v-if="doc?.summaryStatus === 'indexing'"
              class="context-menu-item disabled"
            >
              <i class="fas fa-spinner fa-spin menu-icon icon-generate"></i>
              <span>正在生成索引...</span>
            </div>

            <div
              v-if="doc?.summaryStatus === 'ready'"
              class="context-menu-item disabled"
            >
              <i class="fas fa-check-circle menu-icon icon-ready"></i>
              <span>已索引</span>
            </div>

            <!-- 分隔线 -->
            <div class="context-menu-divider"></div>

            <!-- 文件操作 -->
            <div class="menu-section-label">操作</div>

            <div class="context-menu-item" @click="handleAction('view')">
              <i class="fas fa-eye menu-icon icon-view"></i>
              <span>查看详情</span>
              <span class="menu-shortcut">Enter</span>
            </div>

            <div class="context-menu-item" @click="handleAction('download')">
              <i class="fas fa-download menu-icon icon-download"></i>
              <span>下载 PDF</span>
              <span class="menu-shortcut">Ctrl+S</span>
            </div>

            <div class="context-menu-item danger" @click="handleAction('delete')">
              <i class="fas fa-trash menu-icon icon-delete"></i>
              <span>删除</span>
              <span class="menu-shortcut">Del</span>
            </div>
          </div>
        </div>
      </div>
    </transition>
  </Teleport>
</template>

<script setup lang="ts">
import { ref, onMounted, onUnmounted } from 'vue';
import type { KnowledgeDocument } from '../../stores/knowledgeStore';

export type ContextMenuAction =
  | 'generateIndex'
  | 'retryIndex'
  | 'view'
  | 'download'
  | 'delete';

interface Position {
  x: number;
  y: number;
}

const visible = ref(false);
const position = ref<Position>({ x: 0, y: 0 });
const doc = ref<KnowledgeDocument | null>(null);

const emit = defineEmits<{
  action: [action: ContextMenuAction, doc: KnowledgeDocument];
}>();

function open(event: MouseEvent, document: KnowledgeDocument) {
  event.preventDefault();
  doc.value = document;

  // 计算位置，防止溢出屏幕边缘
  const menuWidth = 260;
  const menuHeight = 340;
  let x = event.clientX;
  let y = event.clientY;

  if (x + menuWidth > window.innerWidth) {
    x = window.innerWidth - menuWidth - 8;
  }
  if (y + menuHeight > window.innerHeight) {
    y = window.innerHeight - menuHeight - 8;
  }

  position.value = { x, y };
  visible.value = true;
}

function close() {
  visible.value = false;
  doc.value = null;
}

function handleAction(action: ContextMenuAction) {
  if (doc.value) {
    emit('action', action, doc.value);
  }
  close();
}

/**
 * 键盘快捷键支持
 */
function onKeyDown(e: KeyboardEvent) {
  if (!visible.value || !doc.value) return;

  switch (e.key) {
    case 'Escape':
      close();
      break;
    case 'Delete':
      handleAction('delete');
      break;
    case 'Enter':
      handleAction('view');
      break;
  }
}

onMounted(() => {
  window.addEventListener('keydown', onKeyDown);
});

onUnmounted(() => {
  window.removeEventListener('keydown', onKeyDown);
});

defineExpose({ open });
</script>

<style scoped>
.context-menu-overlay {
  position: fixed;
  top: 0;
  left: 0;
  right: 0;
  bottom: 0;
  z-index: 9999;
}

.context-menu {
  position: fixed;
  min-width: 260px;
  background: #1e1e3a;
  border: 1px solid rgba(255, 255, 255, 0.12);
  border-radius: 10px;
  box-shadow: 0 8px 32px rgba(0, 0, 0, 0.5);
  overflow: hidden;
  backdrop-filter: blur(12px);
  animation: context-slide-in 0.15s ease-out;
}

@keyframes context-slide-in {
  from {
    opacity: 0;
    transform: scale(0.95) translateY(-4px);
  }
  to {
    opacity: 1;
    transform: scale(1) translateY(0);
  }
}

.context-menu-header {
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 12px 14px;
  background: rgba(255, 255, 255, 0.04);
  border-bottom: 1px solid rgba(255, 255, 255, 0.08);
}

.pdf-icon {
  color: #ef4444;
  font-size: 14px;
}

.file-name {
  font-size: 13px;
  font-weight: 500;
  color: #e0e0e0;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
  flex: 1;
}

.context-menu-body {
  padding: 6px 0;
}

.menu-section-label {
  padding: 6px 14px 4px;
  font-size: 11px;
  font-weight: 600;
  color: #606080;
  text-transform: uppercase;
  letter-spacing: 0.5px;
}

.context-menu-item {
  display: flex;
  align-items: center;
  gap: 10px;
  padding: 8px 14px;
  font-size: 13px;
  color: #d0d0e0;
  cursor: pointer;
  transition: background 0.12s;
  position: relative;
}

.context-menu-item:hover {
  background: rgba(96, 165, 250, 0.12);
}

.context-menu-item.disabled {
  opacity: 0.4;
  cursor: default;
}

.context-menu-item.disabled:hover {
  background: transparent;
}

.context-menu-item.danger {
  color: #ef4444;
}

.context-menu-item.danger:hover {
  background: rgba(239, 68, 68, 0.12);
}

.menu-icon {
  font-size: 13px;
  width: 16px;
  text-align: center;
}

.icon-generate {
  color: #7c3aed;
}

.icon-retry {
  color: #f59e0b;
}

.icon-ready {
  color: #4ade80;
}

.icon-view {
  color: #60a5fa;
}

.icon-download {
  color: #34d399;
}

.icon-delete {
  color: #ef4444;
}

.menu-shortcut {
  margin-left: auto;
  font-size: 11px;
  color: #606080;
  font-family: 'SF Mono', 'Fira Code', monospace;
}

.context-menu-divider {
  height: 1px;
  background: rgba(255, 255, 255, 0.06);
  margin: 4px 0;
}

/* Transition */
.context-fade-enter-active,
.context-fade-leave-active {
  transition: opacity 0.12s ease;
}

.context-fade-enter-from,
.context-fade-leave-to {
  opacity: 0;
}
</style>
