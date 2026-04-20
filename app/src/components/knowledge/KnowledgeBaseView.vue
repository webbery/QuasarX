<template>
  <div
    class="knowledge-container"
    @dragover.prevent="onDragOver"
    @dragleave.prevent="onDragLeave"
    @drop.prevent="onDrop"
    :class="{ 'drag-over': store.isDragging }"
  >
    <!-- 标题栏 -->
    <div class="knowledge-header">
      <h2 class="knowledge-title">
        <i class="fas fa-book-open"></i>
        知识库
      </h2>
      <div class="header-actions">
        <button class="btn-export" @click="onExportSelected" :disabled="selectedCount === 0">
          <i class="fas fa-file-export"></i> 导出选中 ({{ selectedCount }})
        </button>
        <button class="btn-export" @click="onExportAll">
          <i class="fas fa-file-pdf"></i> 全部导出
        </button>
      </div>
    </div>

    <!-- 文档列表表格 -->
    <div class="table-wrapper">
      <table class="doc-table">
        <thead>
          <tr>
            <th class="col-checkbox">
              <input
                type="checkbox"
                :checked="isAllSelected"
                @change="onToggleSelectAll"
              />
            </th>
            <th class="col-filename">文件名</th>
            <th
              class="col-uploadtime"
              :class="{ 'sort-active': store.sortBy === 'uploadTime' }"
              @click="store.setSortBy('uploadTime')"
            >
              上传时间
              <span class="sort-icon">
                <i
                  v-if="store.sortBy === 'uploadTime'"
                  :class="store.sortOrder === 'asc' ? 'fas fa-sort-up' : 'fas fa-sort-down'"
                ></i>
                <i v-else class="fas fa-sort"></i>
              </span>
            </th>
            <th class="col-pages">页数</th>
            <th
              class="col-hitcount"
              :class="{ 'sort-active': store.sortBy === 'hitCount' }"
              @click="store.setSortBy('hitCount')"
            >
              命中次数
              <span class="sort-icon">
                <i
                  v-if="store.sortBy === 'hitCount'"
                  :class="store.sortOrder === 'asc' ? 'fas fa-sort-up' : 'fas fa-sort-down'"
                ></i>
                <i v-else class="fas fa-sort"></i>
              </span>
            </th>
            <th class="col-status">状态</th>
            <th class="col-actions">操作</th>
          </tr>
        </thead>
        <tbody>
          <tr v-for="doc in store.paginatedDocuments" :key="doc.id" class="doc-row">
            <td class="col-checkbox">
              <input
                type="checkbox"
                :checked="store.selectedDocs.has(doc.id)"
                @change="store.toggleSelect(doc.id)"
              />
            </td>
            <td class="col-filename">
              <div class="file-name">
                <i class="fas fa-file-pdf pdf-icon"></i>
                <span class="file-name-text" :title="doc.fileName">{{ doc.title }}</span>
              </div>
            </td>
            <td class="col-uploadtime">{{ formatTime(doc.uploadTime) }}</td>
            <td class="col-pages">{{ doc.pages }}</td>
            <td class="col-hitcount">{{ doc.hitCount }}</td>
            <td class="col-status">
              <span
                class="status-badge"
                :class="{
                  'status-parsing': doc.status === 'parsing',
                  'status-ready': doc.status === 'ready',
                  'status-error': doc.status === 'error',
                }"
              >
                <template v-if="doc.status === 'parsing'">
                  <i class="fas fa-spinner fa-spin"></i> 解析中
                </template>
                <template v-else-if="doc.status === 'ready'">
                  <i class="fas fa-check-circle"></i> 已完成
                </template>
                <template v-else>
                  <i class="fas fa-exclamation-circle"></i> 错误
                </template>
              </span>
            </td>
            <td class="col-actions">
              <button class="action-btn" @click="onViewDetail(doc)" title="查看详情">
                <i class="fas fa-eye"></i>
              </button>
              <button class="action-btn" @click="onDownload(doc)" title="下载 PDF">
                <i class="fas fa-download"></i>
              </button>
              <button class="action-btn btn-delete" @click="onDelete(doc)" title="删除">
                <i class="fas fa-trash"></i>
              </button>
            </td>
          </tr>
          <tr v-if="store.paginatedDocuments.length === 0">
            <td colspan="7" class="empty-row">
              <div class="empty-message">
                <i class="fas fa-inbox"></i>
                <p>暂无文档</p>
              </div>
            </td>
          </tr>
        </tbody>
      </table>
    </div>

    <!-- 拖拽提示区 -->
    <div class="drop-zone">
      <i class="fas fa-cloud-upload-alt"></i>
      <p>拖拽 PDF 文件到此处添加新文档</p>
    </div>

    <!-- 分页 -->
    <div class="pagination-wrapper">
      <div class="pagination-info">
        共 {{ store.totalDocuments }} 条，每页 {{ store.pageSize }} 条
      </div>
      <div class="pagination-controls">
        <button
          class="page-btn"
          :disabled="store.currentPage <= 1"
          @click="store.currentPage -= 1"
        >
          <i class="fas fa-chevron-left"></i> 上一页
        </button>
        <span class="page-indicator">{{ store.currentPage }} / {{ store.totalPages || 1 }}</span>
        <button
          class="page-btn"
          :disabled="store.currentPage >= store.totalPages"
          @click="store.currentPage += 1"
        >
          下一页 <i class="fas fa-chevron-right"></i>
        </button>
      </div>
    </div>

    <!-- 文档详情弹窗 -->
    <DocumentDetailDialog
      v-model:visible="showDetailDialog"
      :document="selectedDocument"
    />
  </div>
</template>

<script setup lang="ts">
import { ref, computed, onMounted } from 'vue';
import { useKnowledgeStore } from '../../stores/knowledgeStore';
import type { KnowledgeDocument } from '../../stores/knowledgeStore';
import { savePdf, listPdfs, deletePdf, downloadPdf } from '../../lib/pdfFileManager';
import { parsePdf } from '../../lib/pdfParser';
import { storeChunks } from '../../lib/vectorDB';
import DocumentDetailDialog from './DocumentDetailDialog.vue';
import { message } from '../../tool';

const store = useKnowledgeStore();
const showDetailDialog = ref(false);
const selectedDocument = ref<any>(null);

const selectedCount = computed(() => store.selectedDocs.size);
const isAllSelected = computed(() => {
  const pageDocs = store.paginatedDocuments;
  return pageDocs.length > 0 && pageDocs.every(d => store.selectedDocs.has(d.id));
});

/**
 * 格式化时间
 */
function formatTime(timestamp: number): string {
  const date = new Date(timestamp);
  const month = String(date.getMonth() + 1).padStart(2, '0');
  const day = String(date.getDate()).padStart(2, '0');
  const hours = String(date.getHours()).padStart(2, '0');
  const minutes = String(date.getMinutes()).padStart(2, '0');
  return `${month}/${day} ${hours}:${minutes}`;
}

/**
 * 拖拽事件处理
 */
function onDragOver() {
  store.setDragging(true);
}

function onDragLeave() {
  store.setDragging(false);
}

async function onDrop(event: DragEvent) {
  store.setDragging(false);
  const files = event.dataTransfer?.files;
  if (!files || files.length === 0) return;

  for (let i = 0; i < files.length; i++) {
    const file = files[i];
    if (!file.name.toLowerCase().endsWith('.pdf')) {
      message.warning(`"${file.name}" 不是 PDF 文件，已跳过`);
      continue;
    }

    // 创建文档记录
    const docId = `doc_${Date.now()}_${i}`;
    store.addDocument({
      id: docId,
      fileName: file.name,
      title: file.name.replace(/\.pdf$/i, ''),
      uploadTime: Date.now(),
      pages: 0,
      status: 'parsing',
      summary: '',
      hitCount: 0,
      chunks: [],
    });

    // 异步解析和保存
    processPdfFile(file, docId);
  }
}

/**
 * 处理 PDF 文件：保存到本地 + 解析 + 存储向量
 */
async function processPdfFile(file: File, docId: string) {
  try {
    // 1. 读取文件
    const arrayBuffer = await file.arrayBuffer();

    // 2. 保存到 knowledge 目录
    const saveResult = await savePdf(arrayBuffer, file.name);
    if (!saveResult.success) {
      store.updateDocumentStatus(docId, 'error', '保存文件失败');
      message.error(`保存 "${file.name}" 失败`);
      return;
    }

    // 3. 解析 PDF
    const parseResult = await parsePdf(arrayBuffer);
    if (!parseResult.text || parseResult.text === '[PDF 解析失败]') {
      store.updateDocumentStatus(docId, 'error', '解析 PDF 失败');
      message.error(`解析 "${file.name}" 失败`);
      return;
    }

    // 4. 存储向量
    await storeChunks(docId, saveResult.fileName, parseResult.chunks);

    // 5. 更新文档状态为 ready
    store.updateDocumentStatus(
      docId,
      'ready',
      parseResult.summary,
      parseResult.chunks,
      parseResult.pages
    );

    message.success(`"${file.name}" 解析完成`);
  } catch (error: any) {
    console.error('[KnowledgeBase] 处理 PDF 失败:', error);
    store.updateDocumentStatus(docId, 'error', error.message || '未知错误');
    message.error(`处理 "${file.name}" 失败`);
  }
}

/**
 * 查看详情
 */
function onViewDetail(doc: any) {
  selectedDocument.value = doc;
  showDetailDialog.value = true;
}

/**
 * 下载 PDF
 */
async function onDownload(doc: any) {
  try {
    await downloadPdf(doc.fileName);
    message.success(`"${doc.title}" 开始下载`);
  } catch (error: any) {
    message.error(`下载失败: ${error.message}`);
  }
}

/**
 * 删除文档
 */
async function onDelete(doc: any) {
  if (!confirm(`确定要删除 "${doc.title}" 吗？`)) return;

  try {
    // 删除本地文件
    await deletePdf(doc.fileName);
    // 删除 store 记录
    store.removeDocument(doc.id);
    message.success(`已删除 "${doc.title}"`);
  } catch (error: any) {
    message.error(`删除失败: ${error.message}`);
  }
}

/**
 * 全选/取消全选
 */
function onToggleSelectAll() {
  store.toggleSelectAll();
}

/**
 * 导出选中
 */
async function onExportSelected() {
  const selected = store.paginatedDocuments.filter(d => store.selectedDocs.has(d.id));
  if (selected.length === 0) {
    message.warning('请先选中要导出的文档');
    return;
  }
  for (const doc of selected) {
    await onDownload(doc);
  }
  message.success(`已导出 ${selected.length} 个文档`);
}

/**
 * 全部导出
 */
async function onExportAll() {
  if (store.totalDocuments === 0) {
    message.warning('知识库为空');
    return;
  }
  for (const doc of store.sortedDocuments) {
    await onDownload(doc);
  }
  message.success(`已导出 ${store.totalDocuments} 个文档`);
}

/**
 * 组件挂载时加载已有 PDF 列表
 */
onMounted(async () => {
  try {
    const result = await listPdfs();
    if (result.success && result.pdfs.length > 0) {
      const existingDocs: KnowledgeDocument[] = result.pdfs.map((pdf) => ({
        id: `doc_${pdf.name}_${pdf.mtime}`,
        fileName: pdf.name,
        title: pdf.name.replace(/\.pdf$/i, ''),
        uploadTime: pdf.mtime,
        pages: 0,
        status: 'ready' as const,
        summary: '（未解析，建议重新处理）',
        hitCount: 0,
        chunks: [],
      }));
      store.addDocuments(existingDocs);
    }
  } catch (error) {
    console.error('[KnowledgeBase] 加载 PDF 列表失败:', error);
  }
});
</script>

<style scoped>
.knowledge-container {
  display: flex;
  flex-direction: column;
  height: 100%;
  padding: 16px;
  background: #1a1a2e;
  color: #e0e0e0;
  border-radius: 12px;
  overflow: hidden;
  transition: all 0.3s ease;
}

.knowledge-container.drag-over {
  border: 2px dashed #60a5fa;
  background: rgba(96, 165, 250, 0.1);
}

/* ========== Header ========== */
.knowledge-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 16px;
  padding: 0 4px;
}

.knowledge-title {
  font-size: 20px;
  font-weight: 600;
  color: #f0f0f0;
  display: flex;
  align-items: center;
  gap: 10px;
  margin: 0;
}

.knowledge-title i {
  color: #60a5fa;
}

.header-actions {
  display: flex;
  gap: 10px;
}

.btn-export {
  background: linear-gradient(90deg, #2563eb, #1d4ed8);
  color: white;
  border: none;
  padding: 8px 16px;
  border-radius: 8px;
  font-size: 13px;
  font-weight: 500;
  cursor: pointer;
  transition: all 0.3s ease;
  display: flex;
  align-items: center;
  gap: 6px;
}

.btn-export:hover:not(:disabled) {
  background: linear-gradient(90deg, #1d4ed8, #1e40af);
  transform: translateY(-1px);
}

.btn-export:disabled {
  opacity: 0.4;
  cursor: not-allowed;
}

/* ========== Table ========== */
.table-wrapper {
  flex: 1;
  overflow-y: auto;
  border: 1px solid rgba(255, 255, 255, 0.08);
  border-radius: 8px;
}

.doc-table {
  width: 100%;
  border-collapse: collapse;
  font-size: 13px;
}

.doc-table thead {
  position: sticky;
  top: 0;
  z-index: 1;
  background: #16213e;
}

.doc-table th {
  padding: 12px 10px;
  text-align: left;
  font-weight: 600;
  color: #a0a0c0;
  border-bottom: 1px solid rgba(255, 255, 255, 0.06);
  cursor: pointer;
  user-select: none;
  transition: color 0.2s;
}

.doc-table th:hover {
  color: #f0f0f0;
}

.doc-table th.sort-active {
  color: #60a5fa;
}

.sort-icon {
  margin-left: 6px;
  font-size: 11px;
}

.doc-table td {
  padding: 10px;
  border-bottom: 1px solid rgba(255, 255, 255, 0.04);
  vertical-align: middle;
}

.doc-row:hover {
  background: rgba(96, 165, 250, 0.05);
}

/* Columns */
.col-checkbox {
  width: 40px;
  text-align: center;
}

.col-filename {
  min-width: 200px;
}

.col-uploadtime {
  width: 120px;
}

.col-pages {
  width: 70px;
  text-align: center;
}

.col-hitcount {
  width: 100px;
  text-align: center;
}

.col-status {
  width: 100px;
}

.col-actions {
  width: 120px;
  text-align: center;
}

/* File name */
.file-name {
  display: flex;
  align-items: center;
  gap: 8px;
}

.pdf-icon {
  color: #ef4444;
  font-size: 14px;
}

.file-name-text {
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
  max-width: 300px;
}

/* Status badges */
.status-badge {
  display: inline-flex;
  align-items: center;
  gap: 4px;
  padding: 3px 8px;
  border-radius: 12px;
  font-size: 12px;
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

/* Action buttons */
.col-actions {
  display: flex;
  gap: 4px;
  justify-content: center;
}

.action-btn {
  background: transparent;
  border: none;
  color: #a0a0c0;
  cursor: pointer;
  padding: 6px;
  border-radius: 6px;
  transition: all 0.2s;
  font-size: 13px;
}

.action-btn:hover {
  background: rgba(255, 255, 255, 0.08);
  color: #f0f0f0;
}

.btn-delete:hover {
  background: rgba(239, 68, 68, 0.15);
  color: #ef4444;
}

/* Empty row */
.empty-row {
  text-align: center;
  padding: 40px 0;
}

.empty-message {
  color: #606080;
}

.empty-message i {
  font-size: 32px;
  margin-bottom: 8px;
}

/* ========== Drop Zone ========== */
.drop-zone {
  margin-top: 12px;
  padding: 20px;
  border: 2px dashed rgba(96, 165, 250, 0.3);
  border-radius: 8px;
  text-align: center;
  color: #606080;
  transition: all 0.3s;
}

.drop-zone i {
  font-size: 24px;
  margin-bottom: 6px;
  color: #60a5fa;
}

.drop-zone p {
  margin: 0;
  font-size: 14px;
}

.knowledge-container.drag-over .drop-zone {
  border-color: #60a5fa;
  background: rgba(96, 165, 250, 0.08);
  color: #60a5fa;
}

/* ========== Pagination ========== */
.pagination-wrapper {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-top: 12px;
  padding: 8px 4px;
}

.pagination-info {
  font-size: 13px;
  color: #a0a0c0;
}

.pagination-controls {
  display: flex;
  align-items: center;
  gap: 8px;
}

.page-btn {
  background: rgba(255, 255, 255, 0.06);
  border: 1px solid rgba(255, 255, 255, 0.1);
  color: #e0e0e0;
  padding: 6px 12px;
  border-radius: 6px;
  cursor: pointer;
  font-size: 12px;
  transition: all 0.2s;
  display: flex;
  align-items: center;
  gap: 4px;
}

.page-btn:hover:not(:disabled) {
  background: rgba(96, 165, 250, 0.15);
  border-color: #60a5fa;
}

.page-btn:disabled {
  opacity: 0.3;
  cursor: not-allowed;
}

.page-indicator {
  font-size: 13px;
  color: #a0a0c0;
  min-width: 80px;
  text-align: center;
}

/* Checkbox style */
input[type="checkbox"] {
  accent-color: #60a5fa;
  cursor: pointer;
}
</style>
