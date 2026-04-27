/**
 * 向量数据库（主进程）
 * 使用 LanceDB 存储 + @xenova/transformers 生成 embedding
 */

import { mkdirSync, existsSync, rmSync } from 'fs';
import { join } from 'path';
import * as lancedb from '@lancedb/lancedb';
import { pipeline, env } from '@xenova/transformers';

const EMBEDDING_MODEL = 'Xenova/paraphrase-multilingual-MiniLM-L12-v2';
const EMBEDDING_DIM = 384;

let db: lancedb.Connection | null = null;
let table: lancedb.Table | null = null;
let extractor: any = null;
let dbDir: string = '';

/**
 * 获取或初始化 embedding extractor
 */
async function getExtractor() {
  if (!extractor) {
    console.log('[VectorDB] loading embedding model...');
    extractor = await pipeline('feature-extraction', EMBEDDING_MODEL, {
      quantized: true, // 使用量化模型减少内存占用
    });
    console.log('[VectorDB] embedding model loaded');
  }
  return extractor;
}

/**
 * 生成文本向量
 */
async function embed(text: string): Promise<number[]> {
  const ext = await getExtractor();
  const output = await ext(text, { pooling: 'mean', normalize: true });
  // output 是 Tensor，转为数组
  return Array.from(output.data);
}

/**
 * 初始化向量数据库
 */
export async function initVectorDB(baseDir: string) {
  dbDir = join(baseDir, 'lancedb');
  mkdirSync(dbDir, { recursive: true });

  // 连接 LanceDB
  db = await lancedb.connect(dbDir);

  // 检查或创建表
  const tableNames = await db.tableNames();
  if (tableNames.includes('chunks')) {
    table = await db.openTable('chunks');
    console.log('[VectorDB] opened existing chunks table');
  } else {
    // 创建空表（需要一个初始向量来定义 schema）
    const emptyData = [
      {
        id: '__init__',
        doc_id: '__init__',
        file_name: '__init__',
        chunk_index: 0,
        content: '__init__',
        vector: new Float32Array(EMBEDDING_DIM),
        metadata: '{}',
      },
    ];
    table = await db.createTable('chunks', emptyData);
    // 删除初始化行
    await table.delete("id = '__init__'");
    console.log('[VectorDB] created chunks table');
  }
}

/**
 * 存储文档的文本块
 */
export async function storeChunks(
  docId: string,
  fileName: string,
  chunksArr: { index: number; content: string }[]
): Promise<void> {
  if (!table) throw new Error('VectorDB not initialized');

  const rows: any[] = [];
  for (const chunk of chunksArr) {
    const id = `${docId}_${chunk.index}`;
    const embedding = await embed(chunk.content);

    rows.push({
      id,
      doc_id: docId,
      file_name: fileName,
      chunk_index: chunk.index,
      content: chunk.content,
      vector: new Float32Array(embedding),
      metadata: JSON.stringify({}),
    });
  }

  if (rows.length > 0) {
    await table.add(rows);
    console.log(`[VectorDB] stored ${rows.length} chunks for ${fileName}`);
  }
}

/**
 * 删除指定文档的所有向量
 */
export async function deleteChunks(docId: string): Promise<void> {
  if (!table) throw new Error('VectorDB not initialized');

  const filter = `doc_id = '${docId}'`;
  await table.delete(filter);
  console.log(`[VectorDB] deleted chunks for doc: ${docId}`);
}

/**
 * 向量检索
 */
export async function vectorSearch(
  queryText: string,
  topK: number = 5
): Promise<any[]> {
  if (!table) throw new Error('VectorDB not initialized');

  const queryEmbedding = await embed(queryText);
  const queryVector = new Float32Array(queryEmbedding);

  // LanceDB 向量检索
  const results = await table
    .vectorSearch(queryVector)
    .limit(topK)
    .select(['id', 'doc_id', 'file_name', 'chunk_index', 'content', 'metadata']);

  // 转换为兼容的返回格式
  const searchResults: any[] = [];
  const rows = await results.toArray();
  for (const row of rows) {
    searchResults.push({
      chunk: {
        id: row.id,
        docId: row.doc_id,
        fileName: row.file_name,
        chunkIndex: row.chunk_index,
        content: row.content,
        embedding: [], // LanceDB 不返回原始向量，除非显式 select
        metadata: JSON.parse(row.metadata || '{}'),
      },
      similarity: row._distance !== undefined ? 1 - row._distance : 0, // LanceDB 返回的是距离，转为相似度
    });
  }

  // 按相似度降序排序
  searchResults.sort((a, b) => b.similarity - a.similarity);
  return searchResults;
}

/**
 * 清空所有向量
 */
export async function clearAll(): Promise<void> {
  if (!db) throw new Error('VectorDB not initialized');

  // LanceDB 没有直接的 drop table，通过 delete 所有数据实现清空
  if (table) {
    // 删除所有行（LanceDB 不支持无条件 delete，需要先查询所有 id）
    const allResults = await table
      .query()
      .select(['id'])
      .toArray();

    if (allResults.length > 0) {
      const ids = allResults.map((r: any) => `'${r.id}'`).join(',');
      await table.delete(`id IN (${ids})`);
    }
    console.log('[VectorDB] cleared all chunks');
  }
}

/**
 * 获取统计信息
 */
export async function getStats(): Promise<{ totalChunks: number; totalDocs: number }> {
  if (!table) throw new Error('VectorDB not initialized');

  // LanceDB 不直接支持 count，需要通过 query 获取
  const allResults = await table.query().select(['doc_id']).toArray();
  const totalChunks = allResults.length;
  const docIds = new Set<string>();
  for (const row of allResults) {
    docIds.add(row.doc_id);
  }

  return {
    totalChunks,
    totalDocs: docIds.size,
  };
}

/**
 * 关闭数据库连接
 */
export async function shutdownVectorDB(): Promise<void> {
  // LanceDB 无需显式关闭连接
  extractor = null;
  table = null;
  db = null;
  console.log('[VectorDB] shutdown');
}

/**
 * 模型缓存路径（在 knowledge 目录下）
 */
function getModelCacheDir(knowledgeDir: string): string {
  // Xenova 模型缓存目录结构：cacheDir/Xenova/<model-name>/
  return join(knowledgeDir, '.model-cache');
}

/**
 * 检测模型是否已缓存
 * 检查关键文件 onnx/model_quantized.onnx 或 onnx/model.onnx 是否存在
 */
function isModelCached(cacheDir: string): boolean {
  const modelDir = join(cacheDir, 'Xenova', 'paraphrase-multilingual-MiniLM-L12-v2', 'onnx');
  // 量化模型优先检查 model_quantized.onnx
  return existsSync(join(modelDir, 'model_quantized.onnx')) || existsSync(join(modelDir, 'model.onnx'));
}

/**
 * 预加载嵌入模型
 * @param knowledgeDir - 知识库目录路径
 * @param onStatus - 可选的状态回调，用于向渲染进程推送进度
 * @returns {Promise<boolean>} - 是否成功加载
 */
export async function preloadModel(
  knowledgeDir: string,
  onStatus?: (status: { type: string; text?: string; progress?: number }) => void
): Promise<boolean> {
  const cacheDir = getModelCacheDir(knowledgeDir);
  env.cacheDir = cacheDir;

  const cached = isModelCached(cacheDir);

  if (cached) {
    // 已缓存，静默加载
    console.log('[VectorDB] model already cached, loading silently...');
    try {
      extractor = await pipeline('feature-extraction', EMBEDDING_MODEL, {
        quantized: true,
      });
      console.log('[VectorDB] model preloaded from cache');
      return true;
    } catch (e) {
      console.error('[VectorDB] failed to load cached model:', e);
      // 缓存损坏，回退到重新下载
      console.log('[VectorDB] cache may be corrupted, will re-download');
    }
  }

  // 未缓存，启动下载并上报进度
  console.log('[VectorDB] model not cached, starting download...');
  if (onStatus) onStatus({ type: 'downloading', text: '开始下载嵌入模型...' });

  let lastProgressText = '';
  let lastProgress = 0;

  try {
    extractor = await pipeline('feature-extraction', EMBEDDING_MODEL, {
      quantized: true,
      progress_callback: (progress: any) => {
        if (progress.status === 'download' && onStatus) {
          lastProgressText = `正在下载 ${progress.file || ''}`;
          onStatus({ type: 'downloading', text: lastProgressText });
        } else if (progress.status === 'progress' && onStatus) {
          lastProgress = Math.round(progress.progress || 0);
          onStatus({ type: 'progress', progress: lastProgress, text: `${lastProgress}% ${progress.file || ''}` });
        } else if (progress.status === 'done' && onStatus) {
          onStatus({ type: 'downloading', text: `已加载 ${progress.file || ''}` });
        } else if (progress.status === 'ready') {
          console.log('[VectorDB] model download complete');
          if (onStatus) onStatus({ type: 'ready' });
        }
      },
    });

    console.log('[VectorDB] model preloaded successfully');
    if (onStatus) onStatus({ type: 'ready' });
    return true;
  } catch (e: any) {
    console.error('[VectorDB] model preload failed:', e.message);
    if (onStatus) onStatus({ type: 'error', text: `模型加载失败: ${e.message}` });
    extractor = null;
    return false;
  }
}
