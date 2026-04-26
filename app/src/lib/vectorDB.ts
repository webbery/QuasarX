/**
 * 向量数据库客户端（IPC 包装器）
 * 渲染进程通过 IPC 与主进程的 PGlite 通信
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
 * 存储文档的文本块及其嵌入向量
 * @param docId 文档 ID
 * @param fileName 文件名
 * @param chunks 文本块数组
 */
export async function storeChunks(
  docId: string,
  fileName: string,
  chunks: { index: number; content: string }[]
): Promise<void> {
  const result = await ipcRenderer.invoke('vector-store-chunks', { docId, fileName, chunks });
  if (!result.success) throw new Error(result.error);
}

/**
 * 删除文档相关的所有向量数据
 * @param docId 文档 ID
 */
export async function deleteChunks(docId: string): Promise<void> {
  const result = await ipcRenderer.invoke('vector-delete-chunks', docId);
  if (!result.success) throw new Error(result.error);
}

/**
 * 向量检索：根据查询文本检索最相似的文本块
 * @param queryText 查询文本
 * @param topK 返回结果数量
 * @returns 搜索结果列表
 */
export async function search(
  queryText: string,
  topK: number = 5
): Promise<SearchResult[]> {
  const result = await ipcRenderer.invoke('vector-search', { queryText, topK });
  if (!result.success) throw new Error(result.error);
  return result.results;
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
export async function getStats(): Promise<{ totalChunks: number; totalDocs: number }> {
  const result = await ipcRenderer.invoke('vector-get-stats');
  if (!result.success) throw new Error(result.error);
  return { totalChunks: result.totalChunks, totalDocs: result.totalDocs };
}
