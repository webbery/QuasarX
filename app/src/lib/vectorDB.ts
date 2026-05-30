/**
 * 向量数据库客户端（IPC 包装器）
 * 渲染进程通过 IPC 与主进程的 LanceDB 通信
 */

import { ipcRenderer } from 'electron';

export interface VectorChunk {
  id: string;           // 唯一 ID
  docId: string;        // 所属文档 ID
  fileName: string;     // 文件名
  chunkIndex: number;   // 块索引
  content: string;      // 文本内容
  embedding: number[];  // 向量嵌入
  metadata: Record<string, any>;
}

export interface SearchResult {
  chunk: VectorChunk;
  similarity: number;  // 相似度 (0-1)
}

/**
 * 摘要搜索结果（新模式）
 */
export interface SummarySearchResult {
  docId: string;
  fileName: string;
  summary: string;
  chunks: VectorChunk[];
  similarity: number;
  mode: 'summary' | 'fallback';
  tags?: string[];
}

/**
 * 存储文档的文本块及其嵌入向量
 * @param docId 文档 ID
 * @param fileName 文件名
 * @param chunks 文本块数组
 * @param pages 文件页数（可选）
 */
export async function storeChunks(
  docId: string,
  fileName: string,
  chunks: { index: number; content: string }[],
  pages?: number
): Promise<void> {
  const result = await ipcRenderer.invoke('vector-store-chunks', { docId, fileName, chunks, pages });
  if (!result.success) throw new Error(result.error);
}

/**
 * 删除文档相关的所有向量数据（包括 chunks 和 summary）
 * @param docId 文档 ID
 */
export async function deleteChunks(docId: string): Promise<void> {
  const result = await ipcRenderer.invoke('vector-delete-chunks', docId);
  if (!result.success) throw new Error(result.error);
}

/**
 * 向量检索：根据查询文本检索最相似的文档
 * 新模式：返回 { docId, fileName, summary, chunks[], similarity, tags, mode }
 * 兼容旧模式：返回 { chunk, similarity }
 */
export async function search(
  queryText: string,
  topK: number = 5,
  tags?: string[]
): Promise<SummarySearchResult[]> {
  const result = await ipcRenderer.invoke('vector-search', { queryText, topK, tags });
  if (!result.success) throw new Error(result.error);
  return result.results;
}

/**
 * 异步生成文档摘要
 */
export async function generateSummary(params: {
  docId: string;
  fileName: string;
  fullText: string;
  chunkIds: string[];
  llmConfig: { url: string; protocol: string; apiKey: string; model: string };
}): Promise<{ success: boolean; summary?: string; error?: string }> {
  const result = await ipcRenderer.invoke('generate-summary', params);
  return result;
}

/**
 * 重试生成文档摘要
 */
export async function retrySummary(params: {
  docId: string;
  fileName: string;
  fullText: string;
  chunkIds: string[];
  llmConfig: { url: string; protocol: string; apiKey: string; model: string };
  pages?: number;
}): Promise<{ success: boolean; summary?: string; tags?: string[]; error?: string }> {
  const result = await ipcRenderer.invoke('retry-summary', params);
  return result;
}

/**
 * 获取文档摘要状态
 */
export async function getSummaryStatus(docIds: string[]): Promise<{
  success: boolean;
  statuses: Record<string, { exists: boolean; status?: string; summary?: string; tags?: string[]; pages?: number }>;
}> {
  const result = await ipcRenderer.invoke('get-summary-status', docIds);
  return result;
}

/**
 * 清空所有向量数据
 */
export async function clearAll(): Promise<void> {
  const result = await ipcRenderer.invoke('vector-clear-all');
  if (!result.success) throw new Error(result.error);
}

/**
 * 获取向量数据统计信息
 */
export async function getStats(): Promise<{ totalChunks: number; totalDocs: number; totalSummaries: number }> {
  const result = await ipcRenderer.invoke('vector-get-stats');
  if (!result.success) throw new Error(result.error);
  return { totalChunks: result.totalChunks, totalDocs: result.totalDocs, totalSummaries: result.totalSummaries };
}

/**
 * 获取指定文档的页数（从 chunks 表的 metadata 中读取）
 */
export async function getPages(docId: string): Promise<number> {
  const result = await ipcRenderer.invoke('vector-get-pages', docId);
  if (!result.success) return 0;
  return result.pages || 0;
}

/**
 * 更新文档标签
 */
export async function updateTags(docId: string, tags: string[]): Promise<void> {
  const result = await ipcRenderer.invoke('knowledge-update-tags', { docId, tags });
  if (!result.success) throw new Error(result.error);
}
