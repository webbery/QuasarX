/**
 * 向量数据库（主进程）
 * 轻量文件向量存储，使用 JSON 文件持久化 + 内存向量检索
 */

import { mkdirSync, existsSync, readFileSync, writeFileSync, rmSync } from 'fs';
import { join } from 'path';
import { SentenceTransformersEmbeddingFunction } from '@chroma-core/sentence-transformer';

const EMBEDDING_DIM = 1024; // bge-m3 output dimension

let embeddingFn: SentenceTransformersEmbeddingFunction | null = null;

interface VectorChunk {
  id: string;
  doc_id: string;
  file_name: string;
  chunk_index: number;
  content: string;
  embedding: number[];
  metadata: Record<string, any>;
}

let chunks: Map<string, VectorChunk> = new Map();
let dbFile: string = '';
let saveTimer: NodeJS.Timeout | null = null;

/**
 * Generate embedding using bge-m3 model (fallback to simpleEmbed if model not loaded)
 */
async function embed(text: string): Promise<number[]> {
  if (embeddingFn) {
    const embeddings = await embeddingFn.generate([text]);
    return embeddings[0];
  }
  // Fallback: use simpleEmbed when model is not available
  return simpleEmbed(text);
}

/**
 * 简单的文本嵌入（基于 hash 的轻量方案，仅作为 fallback）
 */
function simpleEmbed(text: string): number[] {
  const dim = EMBEDDING_DIM;
  const embedding = new Float32Array(dim);
  const words = text.toLowerCase().split(/\s+/);

  for (const word of words) {
    if (word.length < 2) continue;
    let hash = 0;
    for (let i = 0; i < word.length; i++) {
      hash = ((hash << 5) - hash + word.charCodeAt(i)) | 0;
    }
    const index = Math.abs(hash) % dim;
    embedding[index] += 1;
  }

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
 * 延迟保存（防抖 500ms）
 */
function scheduleSave() {
  if (saveTimer) clearTimeout(saveTimer);
  saveTimer = setTimeout(() => {
    try {
      const data = JSON.stringify(Array.from(chunks.values()));
      writeFileSync(dbFile, data, 'utf-8');
    } catch (e) {
      console.error('[VectorDB] save failed:', e);
    }
  }, 500);
}

export async function initVectorDB(baseDir: string) {
  const dbPath = join(baseDir, 'vector-db');
  mkdirSync(dbPath, { recursive: true });
  dbFile = join(dbPath, 'chunks.json');

  // Load bge-m3 model (auto-downloads ~2.2GB on first use, then cached)
  console.log('[VectorDB] loading bge-m3 embedding model...');
  try {
    embeddingFn = new SentenceTransformersEmbeddingFunction({
      modelName: 'bge-m3',
      device: 'cpu',
      normalizeEmbeddings: true,
    });
    console.log('[VectorDB] bge-m3 model loaded');
  } catch (e) {
    console.warn('[VectorDB] failed to load bge-m3, falling back to simpleEmbed:', e);
    embeddingFn = null;
  }

  // Detect old data with mismatched dimension and clear it
  if (existsSync(dbFile)) {
    try {
      const data = readFileSync(dbFile, 'utf-8');
      const parsed = JSON.parse(data) as VectorChunk[];
      if (parsed.length > 0 && parsed[0].embedding.length !== EMBEDDING_DIM) {
        console.warn(`[VectorDB] dimension mismatch (${parsed[0].embedding.length} != ${EMBEDDING_DIM}), clearing old data`);
        if (existsSync(dbFile)) rmSync(dbFile);
        chunks = new Map();
      } else {
        chunks = new Map(parsed.map(c => [c.id, c]));
        console.log(`[VectorDB] loaded ${chunks.size} chunks from disk`);
      }
    } catch (e) {
      console.error('[VectorDB] failed to load chunks:', e);
      chunks = new Map();
    }
  } else {
    console.log('[VectorDB] initialized (new)');
  }
}

export async function storeChunks(
  docId: string,
  fileName: string,
  chunksArr: { index: number; content: string }[]
): Promise<void> {
  for (const chunk of chunksArr) {
    const id = `${docId}_${chunk.index}`;
    const embedding = await embed(chunk.content);

    chunks.set(id, {
      id,
      doc_id: docId,
      file_name: fileName,
      chunk_index: chunk.index,
      content: chunk.content,
      embedding,
      metadata: {},
    });
  }

  scheduleSave();
}

export async function deleteChunks(docId: string): Promise<void> {
  let deleted = 0;
  for (const [id, chunk] of chunks) {
    if (chunk.doc_id === docId) {
      chunks.delete(id);
      deleted++;
    }
  }
  if (deleted > 0) scheduleSave();
}

export async function vectorSearch(
  queryText: string,
  topK: number = 5
): Promise<any[]> {
  const queryEmbedding = await embed(queryText);

  const results: any[] = [];

  for (const chunk of chunks.values()) {
    const similarity = cosineSimilarity(queryEmbedding, chunk.embedding);
    if (similarity > 0) {
      results.push({
        chunk: {
          id: chunk.id,
          docId: chunk.doc_id,
          fileName: chunk.file_name,
          chunkIndex: chunk.chunk_index,
          content: chunk.content,
          embedding: chunk.embedding,
          metadata: chunk.metadata,
        },
        similarity,
      });
    }
  }

  results.sort((a, b) => b.similarity - a.similarity);
  return results.slice(0, topK);
}

export async function clearAll(): Promise<void> {
  chunks.clear();
  if (existsSync(dbFile)) {
    rmSync(dbFile);
  }
}

export async function getStats(): Promise<{ totalChunks: number; totalDocs: number }> {
  const docIds = new Set<string>();
  for (const chunk of chunks.values()) {
    docIds.add(chunk.doc_id);
  }
  return {
    totalChunks: chunks.size,
    totalDocs: docIds.size,
  };
}

export async function shutdownVectorDB(): Promise<void> {
  // Flush any pending saves
  if (saveTimer) {
    clearTimeout(saveTimer);
    saveTimer = null;
    // Save immediately
    try {
      const data = JSON.stringify(Array.from(chunks.values()));
      writeFileSync(dbFile, data, 'utf-8');
    } catch (e) {
      // ignore on shutdown
    }
  }
  chunks.clear();
}
