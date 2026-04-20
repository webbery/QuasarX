/**
 * 向量数据库（基于 PGlite）
 * 用于存储和检索 PDF 文本块的向量嵌入
 * 数据存储在 knowledge/vector.db 文件中，与 PDF 文件同目录
 */

import { PGlite } from '@electric-sql/pglite';
import { getKnowledgeDir } from './pdfFileManager';
import { join } from 'path';

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

const EMBEDDING_DIM = 384;  // 使用轻量 embedding 模型维度

let dbInstance: PGlite | null = null;

/**
 * 获取向量数据库文件路径
 * 存储在 knowledge/vector.db 目录下
 */
function getDbPath(dataDir: string): string {
  const dbDir = join(dataDir, 'vector.db');
  return `file://${dbDir}`;
}

/**
 * 获取 PGlite 数据库实例
 * 数据库文件存储在 knowledge/vector.db/ 目录下
 */
async function getDB(): Promise<PGlite> {
  if (dbInstance) return dbInstance;

  // 获取 knowledge 目录路径
  const dirResult = await getKnowledgeDir();
  if (!('path' in dirResult)) {
    throw new Error('无法获取知识库目录');
  }

  const dbPath = getDbPath(dirResult.path);
  dbInstance = await PGlite.create(dbPath);

  // 创建表结构
  await dbInstance.exec(`
    CREATE TABLE IF NOT EXISTS vector_chunks (
      id TEXT PRIMARY KEY,
      doc_id TEXT NOT NULL,
      file_name TEXT NOT NULL,
      chunk_index INTEGER NOT NULL,
      content TEXT NOT NULL,
      embedding FLOAT4[],
      metadata JSONB DEFAULT '{}'
    );

    CREATE INDEX IF NOT EXISTS idx_vector_chunks_doc_id ON vector_chunks(doc_id);
  `);

  return dbInstance;
}

/**
 * 简单的文本嵌入（使用 TF-IDF 风格的轻量方案）
 * 实际生产中应调用外部 embedding API
 */
function simpleEmbed(text: string): number[] {
  // 简单的 hash-based 嵌入模拟
  const dim = EMBEDDING_DIM;
  const embedding = new Float32Array(dim);
  const words = text.toLowerCase().split(/\s+/);

  for (const word of words) {
    if (word.length < 2) continue;
    // 使用字符串 hash 映射到向量维度
    let hash = 0;
    for (let i = 0; i < word.length; i++) {
      hash = ((hash << 5) - hash + word.charCodeAt(i)) | 0;
    }
    const index = Math.abs(hash) % dim;
    embedding[index] += 1;
  }

  // 归一化
  let norm = 0;
  for (let i = 0; i < dim; i++) {
    norm += embedding[i] * embedding[i];
  }
  norm = Math.sqrt(norm) || 1;
  for (let i = 0; i < dim; i++) {
    embedding[i] /= norm;
  }

  return Array.from(embedding);
}

/**
 * 计算余弦相似度
 */
function cosineSimilarity(a: number[], b: number[]): number {
  if (a.length !== b.length) return 0;
  let dot = 0, normA = 0, normB = 0;
  for (let i = 0; i < a.length; i++) {
    dot += a[i] * b[i];
    normA += a[i] * a[i];
    normB += b[i] * b[i];
  }
  if (normA === 0 || normB === 0) return 0;
  return dot / (Math.sqrt(normA) * Math.sqrt(normB));
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
  const db = await getDB();

  for (const chunk of chunks) {
    const id = `${docId}_${chunk.index}`;
    const embedding = simpleEmbed(chunk.content);

    await db.query(
      `INSERT INTO vector_chunks (id, doc_id, file_name, chunk_index, content, embedding, metadata)
       VALUES ($1, $2, $3, $4, $5, $6, $7)
       ON CONFLICT (id) DO UPDATE SET content = $5, embedding = $6`,
      [id, docId, fileName, chunk.index, chunk.content, embedding, JSON.stringify({})]
    );
  }
}

/**
 * 删除文档相关的所有向量数据
 * @param docId 文档 ID
 */
export async function deleteChunks(docId: string): Promise<void> {
  const db = await getDB();
  await db.query('DELETE FROM vector_chunks WHERE doc_id = $1', [docId]);
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
  const db = await getDB();
  const queryEmbedding = simpleEmbed(queryText);

  // 获取所有向量进行比对
  const result = await db.query<{
    id: string;
    doc_id: string;
    file_name: string;
    chunk_index: number;
    content: string;
    embedding: number[];
    metadata: string;
  }>('SELECT * FROM vector_chunks');

  const results: SearchResult[] = [];

  for (const row of result.rows) {
    const similarity = cosineSimilarity(queryEmbedding, row.embedding);
    if (similarity > 0) {
      results.push({
        chunk: {
          id: row.id,
          docId: row.doc_id,
          fileName: row.file_name,
          chunkIndex: row.chunk_index,
          content: row.content,
          embedding: row.embedding,
          metadata: JSON.parse(row.metadata),
        },
        similarity,
      });
    }
  }

  // 按相似度排序并取 Top-K
  results.sort((a, b) => b.similarity - a.similarity);
  return results.slice(0, topK);
}

/**
 * 清空所有向量数据
 */
export async function clearAll(): Promise<void> {
  const db = await getDB();
  await db.query('DELETE FROM vector_chunks');
}

/**
 * 获取向量数据统计信息
 */
export async function getStats(): Promise<{ totalChunks: number; totalDocs: number }> {
  const db = await getDB();

  const totalResult = await db.query<{ count: number }>('SELECT COUNT(*) as count FROM vector_chunks');
  const docsResult = await db.query<{ count: number }>('SELECT COUNT(DISTINCT doc_id) as count FROM vector_chunks');

  return {
    totalChunks: Number(totalResult.rows[0]?.count || 0),
    totalDocs: Number(docsResult.rows[0]?.count || 0),
  };
}
