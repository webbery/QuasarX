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
        <button class="btn-index" @click="onGenerateSelected" :disabled="selectedCount === 0">
          <i class="fas fa-magic"></i> 生成索引 ({{ selectedCount }})
        </button>
        <button class="btn-index" @click="onGenerateAll">
          <i class="fas fa-layer-group"></i> 全部生成索引
        </button>
        <button class="btn-export" @click="onExportSelected" :disabled="selectedCount === 0">
          <i class="fas fa-file-export"></i> 导出 ({{ selectedCount }})
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
            <th class="col-tags">标签</th>
            <th class="col-index-status">索引状态</th>
            <th class="col-actions">操作</th>
          </tr>
        </thead>
        <tbody>
          <tr
            v-for="doc in store.paginatedDocuments"
            :key="doc.id"
            class="doc-row"
            @contextmenu.prevent="onContextMenu($event, doc)"
          >
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
            <td class="col-tags">
              <div class="tags-container">
                <span v-for="tag in (doc.tags || [])" :key="tag" class="tag-chip">{{ tag }}</span>
                <button class="btn-edit-tags" @click="onEditTags(doc)" title="编辑标签">
                  <i class="fas fa-pen"></i>
                </button>
              </div>
            </td>
            <td class="col-index-status">
              <!-- 索引状态 -->
              <template v-if="doc.summaryStatus === 'indexing'">
                <span class="index-badge index-pending">
                  <i class="fas fa-spinner fa-spin"></i>
                  <span class="index-text">生成中</span>
                </span>
              </template>
              <template v-else-if="doc.summaryStatus === 'ready'">
                <span class="index-badge index-ready">
                  <i class="fas fa-check-circle"></i>
                  <span class="index-text">已索引</span>
                </span>
              </template>
              <template v-else-if="doc.summaryStatus === 'failed'">
                <span class="index-badge index-failed">
                  <i class="fas fa-exclamation-triangle"></i>
                  <span class="index-text">未生成</span>
                  <button class="btn-retry-inline" @click="onRetryIndex(doc)" title="重新生成">
                    <i class="fas fa-redo"></i>
                  </button>
                </span>
              </template>
              <template v-else>
                <span class="index-badge index-none">
                  <i class="fas fa-clock"></i>
                  <span class="index-text">未索引</span>
                  <button class="btn-retry-inline" @click="onGenerateSingle(doc)" title="生成索引">
                    <i class="fas fa-magic"></i>
                  </button>
                </span>
              </template>
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
            <td colspan="8" class="empty-row">
              <div class="empty-message">
                <i class="fas fa-inbox"></i>
                <p>暂无文档</p>
              </div>
            </td>
          </tr>
        </tbody>
      </table>
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

    <!-- 右键上下文菜单 -->
    <ContextMenu
      ref="contextMenuRef"
      @action="onContextAction"
    />

    <!-- 标签编辑弹窗 -->
    <EditTagsDialog
      v-model:visible="showTagsDialog"
      :document="editingTagsDoc"
      @save="onSaveTags"
    />
  </div>
</template>

<script setup lang="ts">
import { ref, computed, onMounted } from 'vue';
import { useKnowledgeStore } from '../../stores/knowledgeStore';
import type { KnowledgeDocument } from '../../stores/knowledgeStore';
import { savePdf, listPdfs, deletePdf, downloadPdf, calculateFileHash } from '../../lib/pdfFileManager';
import { parsePdf } from '../../lib/pdfParser';
import { storeChunks, deleteChunks, retrySummary, getSummaryStatus, getPages } from '../../lib/vectorDB';
import DocumentDetailDialog from './DocumentDetailDialog.vue';
import ContextMenu from './ContextMenu.vue';
import EditTagsDialog from './EditTagsDialog.vue';
import type { ContextMenuAction } from './ContextMenu.vue';
import { message } from '../../tool';
import { getAgentConfig } from '../../lib/agent';
import { updateTags as updateTagsInDB } from '../../lib/vectorDB';

const store = useKnowledgeStore();
const showDetailDialog = ref(false);
const selectedDocument = ref<any>(null);
const contextMenuRef = ref<InstanceType<typeof ContextMenu> | null>(null);

// 标签编辑
const showTagsDialog = ref(false);
const editingTagsDoc = ref<KnowledgeDocument | null>(null);

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
 * 右键菜单
 */
function onContextMenu(event: MouseEvent, doc: KnowledgeDocument) {
  contextMenuRef.value?.open(event, doc);
}

/**
 * 右键菜单动作分发
 */
function onContextAction(action: ContextMenuAction, doc: KnowledgeDocument) {
  switch (action) {
    case 'generateIndex':
      onGenerateSingle(doc);
      break;
    case 'retryIndex':
      onRetryIndex(doc);
      break;
    case 'view':
      onViewDetail(doc);
      break;
    case 'download':
      onDownload(doc);
      break;
    case 'delete':
      onDelete(doc);
      break;
  }
}

/**
 * 拖拽事件
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

    // ★ 先保存文件，使用服务端返回的 hash（保证一致性）
    const arrayBuffer = await file.arrayBuffer();
    const saveResult = await savePdf(arrayBuffer, file.name);
    if (!saveResult.success) {
      message.error(`保存 "${file.name}" 失败`);
      continue;
    }

    // ★ 使用服务端返回的 hash（与 listPdfs 计算的 hash 一致）
    const fileHash = saveResult.hash;

    // 检查是否已有相同文件
    if (store.documents.some(d => d.fileHash === fileHash)) {
      message.warning(`"${file.name}" 已存在于知识库中，已跳过`);
      continue;
    }

    // ★ 使用文件 hash 作为 docId（保证一致性，天然去重）
    const docId = `doc_${fileHash}`;
    store.addDocument({
      id: docId,
      fileName: saveResult.fileName,
      title: saveResult.fileName.replace(/\.pdf$/i, ''),
      uploadTime: saveResult.mtime || Date.now(),
      pages: 0,
      status: 'parsing',
      summary: '',
      hitCount: 0,
      chunks: [],
      summaryStatus: 'pending',
      fileHash,
      tags: [],
    });

    processPdfFile(file, docId, saveResult);
  }
}

/**
 * 处理 PDF 文件
 */
async function processPdfFile(file: File, docId: string, saveResult?: { fileName: string; path: string; hash?: string; mtime?: number }) {
  try {
    const arrayBuffer = await file.arrayBuffer();

    // 如果 saveResult 没有传进来，尝试保存
    let actualSaveResult = saveResult;
    if (!actualSaveResult) {
      actualSaveResult = await savePdf(arrayBuffer, file.name);
      if (!actualSaveResult.success) {
        store.updateDocumentStatus(docId, 'error', '保存文件失败');
        message.error(`保存 "${file.name}" 失败`);
        return;
      }
    }

    const parseResult = await parsePdf(arrayBuffer);
    if (!parseResult.text || parseResult.text === '[PDF 解析失败]') {
      store.updateDocumentStatus(docId, 'error', '解析 PDF 失败');
      message.error(`解析 "${file.name}" 失败`);
      return;
    }

    const chunkIds = parseResult.chunks.map(c => `${docId}_${c.index}`);
    await storeChunks(docId, actualSaveResult.fileName, parseResult.chunks, parseResult.pages);

    store.setFullText(docId, parseResult.text);

    store.updateDocumentStatus(
      docId,
      'ready',
      parseResult.summary,
      parseResult.chunks,
      parseResult.pages
    );

    // 异步触发摘要生成
    triggerSummaryGeneration(docId, actualSaveResult.fileName, parseResult.text, chunkIds, parseResult.pages);

    message.success(`"${file.name}" 解析完成`);
  } catch (error: any) {
    console.error('[KnowledgeBase] 处理 PDF 失败:', error);
    store.updateDocumentStatus(docId, 'error', error.message || '未知错误');
    message.error(`处理 "${file.name}" 失败`);
  }
}

/**
 * 异步生成摘要
 */
async function triggerSummaryGeneration(
  docId: string,
  fileName: string,
  fullText: string,
  chunkIds: string[],
  pages?: number
) {
  const llmConfig = getAgentConfig();
  if (!llmConfig) {
    store.updateSummaryStatus(docId, 'failed');
    message.warning(`"${fileName}" 未配置 AI 服务，无法生成索引`);
    return;
  }

  try {
    store.updateSummaryStatus(docId, 'indexing');

    const result = await retrySummary({
      docId,
      fileName,
      fullText,
      chunkIds,
      llmConfig,
      pages,
    });

    if (result.success) {
      store.updateSummaryStatus(docId, 'ready');
      // 保存标签
      if (result.tags && result.tags.length > 0) {
        store.updateTags(docId, result.tags);
      }
      // 更新页数
      if (pages && pages > 0) {
        const doc = store.documents.find(d => d.id === docId);
        if (doc) {
          doc.pages = pages;
        }
      }
    } else {
      store.updateSummaryStatus(docId, 'failed');
      message.error(`"${fileName}" 索引生成失败: ${result.error}`);
    }
  } catch (error: any) {
    console.error('[KnowledgeBase] 摘要生成失败:', error);
    store.updateSummaryStatus(docId, 'failed');
    message.error(`"${fileName}" 索引生成失败`);
  }
}

/**
 * 单个文档生成索引
 */
async function onGenerateSingle(doc: KnowledgeDocument) {
  if (!doc.fullText || doc.chunks.length === 0) {
    message.warning(`"${doc.title}" 缺少完整文本，无法生成索引`);
    return;
  }
  const chunkIds = doc.chunks.map(c => `${doc.id}_${c.index}`);
  await triggerSummaryGeneration(doc.id, doc.fileName, doc.fullText, chunkIds);
}

/**
 * 单个文档重试
 */
async function onRetryIndex(doc: KnowledgeDocument) {
  if (!doc.fullText || doc.chunks.length === 0) {
    message.warning(`"${doc.title}" 缺少完整文本，无法重新生成索引`);
    return;
  }
  const llmConfig = getAgentConfig();
  if (!llmConfig) {
    message.warning('未配置 AI 服务，无法生成索引');
    return;
  }

  try {
    store.updateSummaryStatus(doc.id, 'indexing');
    const result = await retrySummary({
      docId: doc.id,
      fileName: doc.fileName,
      fullText: doc.fullText!,
      chunkIds: doc.chunks.map(c => `${doc.id}_${c.index}`),
      llmConfig,
    });

    if (result.success) {
      store.updateSummaryStatus(doc.id, 'ready');
      message.success(`"${doc.title}" 索引重新生成成功`);
    } else {
      store.updateSummaryStatus(doc.id, 'failed');
      message.error(`"${doc.title}" 索引重新生成失败: ${result.error}`);
    }
  } catch (error: any) {
    console.error('[KnowledgeBase] 摘要重试失败:', error);
    store.updateSummaryStatus(doc.id, 'failed');
    message.error(`"${doc.title}" 索引重试失败`);
  }
}

/**
 * 批量生成索引（选中）
 */
async function onGenerateSelected() {
  const selected = store.paginatedDocuments.filter(d => store.selectedDocs.has(d.id));
  if (selected.length === 0) {
    message.warning('请先选中要生成索引的文档');
    return;
  }
  await batchGenerateIndexes(selected);
}

/**
 * 全部生成索引
 */
async function onGenerateAll() {
  if (store.totalDocuments === 0) {
    message.warning('知识库为空');
    return;
  }
  const needIndex = store.sortedDocuments.filter(
    d => d.summaryStatus !== 'ready' && d.summaryStatus !== 'indexing' && d.fullText && d.chunks.length > 0
  );
  if (needIndex.length === 0) {
    message.info('所有文档均已索引');
    return;
  }
  await batchGenerateIndexes(needIndex);
}

/**
 * 批量异步生成索引（并发度 3）
 */
async function batchGenerateIndexes(docs: KnowledgeDocument[]) {
  const llmConfig = getAgentConfig();
  if (!llmConfig) {
    message.warning('未配置 AI 服务，无法生成索引');
    return;
  }

  let successCount = 0;
  let failCount = 0;
  const queue = [...docs];
  const concurrency = 3;

  const worker = async () => {
    while (queue.length > 0) {
      const doc = queue.shift()!;
      if (!doc.fullText || doc.chunks.length === 0) {
        store.updateSummaryStatus(doc.id, 'failed');
        failCount++;
        continue;
      }

      try {
        store.updateSummaryStatus(doc.id, 'indexing');
        const result = await retrySummary({
          docId: doc.id,
          fileName: doc.fileName,
          fullText: doc.fullText,
          chunkIds: doc.chunks.map(c => `${doc.id}_${c.index}`),
          llmConfig,
        });

        if (result.success) {
          store.updateSummaryStatus(doc.id, 'ready');
          successCount++;
        } else {
          store.updateSummaryStatus(doc.id, 'failed');
          failCount++;
        }
      } catch {
        store.updateSummaryStatus(doc.id, 'failed');
        failCount++;
      }
    }
  };

  const workers = Array.from({ length: Math.min(concurrency, docs.length) }, () => worker());
  await Promise.all(workers);

  message.success(`索引生成完成：成功 ${successCount} 个，失败 ${failCount} 个`);
}

/**
 * 查看详情
 */
function onViewDetail(doc: KnowledgeDocument) {
  selectedDocument.value = doc;
  showDetailDialog.value = true;
}

/**
 * 下载 PDF
 */
async function onDownload(doc: KnowledgeDocument) {
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
async function onDelete(doc: KnowledgeDocument) {
  if (!confirm(`确定要删除 "${doc.title}" 吗？`)) return;

  try {
    await deleteChunks(doc.id);
    await deletePdf(doc.fileName);
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
 * 打开标签编辑弹窗
 */
function onEditTags(doc: KnowledgeDocument) {
  editingTagsDoc.value = doc;
  showTagsDialog.value = true;
}

/**
 * 保存标签
 */
async function onSaveTags(doc: KnowledgeDocument, tags: string[]) {
  try {
    await updateTagsInDB(doc.id, tags);
    store.updateTags(doc.id, tags);
    message.success(`"${doc.title}" 标签已更新`);
  } catch (error: any) {
    message.error(`标签更新失败: ${error.message}`);
  }
}

/**
 * 组件挂载时加载 PDF 列表
 */
onMounted(async () => {
  try {
    const result = await listPdfs();
    if (result.success && result.pdfs.length > 0) {
      const docIds: string[] = [];
      const docMap = new Map<string, KnowledgeDocument>();

      // ★ 修复：清空后再添加，避免重复
      store.clearAll();

      for (const pdf of result.pdfs) {
        // ★ 使用文件 hash 作为 docId（与上传时保持一致）
        const docId = `doc_${pdf.hash}`;
        docIds.push(docId);
        const doc: KnowledgeDocument = {
          id: docId,
          fileName: pdf.name,
          title: pdf.name.replace(/\.pdf$/i, ''),
          uploadTime: pdf.mtime,
          pages: 0,  // 初始为 0，后面会从 LanceDB 恢复
          status: 'ready',
          summary: '（未解析，建议重新处理）',
          hitCount: 0,
          chunks: [],
          summaryStatus: 'pending',
          fileHash: pdf.hash,
          tags: [],
        };
        store.addDocument(doc);
        docMap.set(docId, doc);
      }

      // 异步查询摘要状态 + 恢复页数
      try {
        const statusResult = await getSummaryStatus(docIds);
        
        console.log(`[KnowledgeBase] getSummaryStatus result:`, statusResult);
        
        if (statusResult.success) {
          for (const [docId, statusInfo] of Object.entries(statusResult.statuses)) {
            const doc = store.documents.find(d => d.id === docId);
            if (doc) {
              console.log(`[KnowledgeBase] doc ${docId}: exists=${statusInfo.exists}, status=${statusInfo.status}, tags=[${(statusInfo.tags || []).join(', ')}], pages=${statusInfo.pages}`);
              
              if (statusInfo.exists) {
                // ★ 使用 store 方法更新状态，确保响应式生效
                doc.summaryStatus = (statusInfo.status as any) || 'ready';
                store.updateTags(docId, statusInfo.tags || []);
                // ★ 更新页数（直接赋值应该触发响应式，因为 documents 是 reactive array）
                if (statusInfo.pages && statusInfo.pages > 0) {
                  doc.pages = statusInfo.pages;
                  console.log(`[KnowledgeBase] restored pages=${doc.pages} for doc ${docId}`);
                }
              } else {
                doc.summaryStatus = 'pending';
                store.updateTags(docId, []);
              }
            }
          }
        }
      } catch (error) {
        console.error('[KnowledgeBase] 状态查询失败:', error);
        // 状态查询失败不影响列表显示
      }
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

.btn-index {
  background: linear-gradient(90deg, #7c3aed, #6d28d9);
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

.btn-index:hover:not(:disabled) {
  background: linear-gradient(90deg, #6d28d9, #5b21b6);
  transform: translateY(-1px);
}

.btn-index:disabled {
  opacity: 0.4;
  cursor: not-allowed;
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

.col-tags {
  width: 180px;
}

.col-index-status {
  width: 140px;
  text-align: center;
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

/* Index status badges */
.index-badge {
  display: inline-flex;
  align-items: center;
  gap: 5px;
  padding: 4px 10px;
  border-radius: 12px;
  font-size: 12px;
  white-space: nowrap;
}

.index-text {
  margin-right: 2px;
}

.index-pending {
  background: rgba(251, 191, 36, 0.15);
  color: #fbbf24;
}

.index-ready {
  background: rgba(74, 222, 128, 0.15);
  color: #4ade80;
}

.index-failed {
  background: rgba(239, 68, 68, 0.15);
  color: #ef4444;
}

.index-none {
  background: rgba(148, 163, 184, 0.15);
  color: #94a3b8;
}

.btn-retry-inline {
  background: none;
  border: none;
  color: inherit;
  cursor: pointer;
  padding: 2px 4px;
  border-radius: 4px;
  font-size: 11px;
  opacity: 0.7;
  transition: all 0.2s;
  display: inline-flex;
  align-items: center;
}

.btn-retry-inline:hover {
  opacity: 1;
  background: rgba(255, 255, 255, 0.1);
}

/* ========== Tags ========== */
.tags-container {
  display: flex;
  flex-wrap: wrap;
  align-items: center;
  gap: 4px;
}

.tag-chip {
  display: inline-block;
  padding: 2px 8px;
  border-radius: 10px;
  font-size: 11px;
  background: rgba(96, 165, 250, 0.15);
  color: #60a5fa;
  white-space: nowrap;
}

.btn-edit-tags {
  background: none;
  border: none;
  color: #606080;
  cursor: pointer;
  padding: 2px 4px;
  border-radius: 4px;
  font-size: 10px;
  opacity: 0.5;
  transition: all 0.2s;
  display: inline-flex;
  align-items: center;
}

.btn-edit-tags:hover {
  opacity: 1;
  color: #60a5fa;
  background: rgba(96, 165, 250, 0.1);
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

input[type="checkbox"] {
  accent-color: #60a5fa;
  cursor: pointer;
}
</style>
