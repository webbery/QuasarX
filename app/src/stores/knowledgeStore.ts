import { defineStore } from 'pinia'

export interface DocumentChunk {
  index: number;
  content: string;
  embedding?: number[];
}

export interface KnowledgeDocument {
  id: string;
  fileName: string;
  title: string;
  uploadTime: number;       // 上传时间戳
  pages: number;
  status: 'parsing' | 'ready' | 'error';
  summary: string;
  hitCount: number;         // 向量检索命中次数
  chunks: DocumentChunk[];
}

export type SortField = 'uploadTime' | 'hitCount';
export type SortOrder = 'asc' | 'desc';

export interface KnowledgeState {
  documents: KnowledgeDocument[];
  sortBy: SortField;
  sortOrder: SortOrder;
  currentPage: number;
  pageSize: number;
  isDragging: boolean;
  selectedDocs: Set<string>;
}

export const useKnowledgeStore = defineStore('knowledge', {
  state: (): KnowledgeState => ({
    documents: [],
    sortBy: 'uploadTime',
    sortOrder: 'desc',
    currentPage: 1,
    pageSize: 20,
    isDragging: false,
    selectedDocs: new Set<string>(),
  }),

  getters: {
    /**
     * 排序后的文档列表
     */
    sortedDocuments(): KnowledgeDocument[] {
      const sorted = [...this.documents].sort((a, b) => {
        let cmp = 0;
        if (this.sortBy === 'uploadTime') {
          cmp = a.uploadTime - b.uploadTime;
        } else if (this.sortBy === 'hitCount') {
          cmp = a.hitCount - b.hitCount;
        }
        return this.sortOrder === 'asc' ? cmp : -cmp;
      });
      return sorted;
    },

    /**
     * 总页数
     */
    totalPages(): number {
      return Math.ceil(this.documents.length / this.pageSize);
    },

    /**
     * 当前页的文档
     */
    paginatedDocuments(): KnowledgeDocument[] {
      const start = (this.currentPage - 1) * this.pageSize;
      const end = start + this.pageSize;
      return this.sortedDocuments.slice(start, end);
    },

    /**
     * 总文档数
     */
    totalDocuments(): number {
      return this.documents.length;
    },

    /**
     * 通过 ID 获取文档
     */
    getDocumentById: () => (id: string): KnowledgeDocument | null => {
      const store = useKnowledgeStore();
      return store.documents.find(d => d.id === id) || null;
    },
  },

  actions: {
    /**
     * 添加文档
     */
    addDocument(doc: KnowledgeDocument) {
      this.documents.push(doc);
    },

    /**
     * 批量添加文档
     */
    addDocuments(docs: KnowledgeDocument[]) {
      this.documents.push(...docs);
    },

    /**
     * 移除文档
     */
    removeDocument(id: string) {
      this.documents = this.documents.filter(d => d.id !== id);
      this.selectedDocs.delete(id);
    },

    /**
     * 更新文档状态
     */
    updateDocumentStatus(id: string, status: 'parsing' | 'ready' | 'error', summary?: string, chunks?: DocumentChunk[], pages?: number) {
      const doc = this.documents.find(d => d.id === id);
      if (doc) {
        doc.status = status;
        if (summary !== undefined) doc.summary = summary;
        if (chunks !== undefined) doc.chunks = chunks;
        if (pages !== undefined) doc.pages = pages;
      }
    },

    /**
     * 增加命中次数
     */
    incrementHitCount(id: string) {
      const doc = this.documents.find(d => d.id === id);
      if (doc) {
        doc.hitCount += 1;
      }
    },

    /**
     * 设置排序字段
     */
    setSortBy(field: SortField) {
      if (this.sortBy === field) {
        // 切换排序方向
        this.sortOrder = this.sortOrder === 'asc' ? 'desc' : 'asc';
      } else {
        this.sortBy = field;
        this.sortOrder = 'desc';
      }
    },

    /**
     * 设置当前页
     */
    setCurrentPage(page: number) {
      if (page >= 1 && page <= this.totalPages) {
        this.currentPage = page;
      }
    },

    /**
     * 设置拖拽状态
     */
    setDragging(value: boolean) {
      this.isDragging = value;
    },

    /**
     * 选中/取消选中文档
     */
    toggleSelect(id: string) {
      if (this.selectedDocs.has(id)) {
        this.selectedDocs.delete(id);
      } else {
        this.selectedDocs.add(id);
      }
    },

    /**
     * 全选/取消全选
     */
    toggleSelectAll() {
      if (this.selectedDocs.size === this.paginatedDocuments.length) {
        this.selectedDocs.clear();
      } else {
        this.paginatedDocuments.forEach(d => this.selectedDocs.add(d.id));
      }
    },

    /**
     * 清空所有文档
     */
    clearAll() {
      this.documents = [];
      this.selectedDocs.clear();
      this.currentPage = 1;
    },
  },
});
