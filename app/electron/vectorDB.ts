/**
 * 向量数据库（主进程）
 * 使用 LanceDB 存储 + @xenova/transformers 生成 embedding
 */

import { mkdirSync, existsSync, rmSync } from 'fs';
import { join } from 'path';
import * as lancedb from '@lancedb/lancedb';
import { pipeline, env } from '@xenova/transformers';

const EMBEDDING_MODEL = 'Xenova/distiluse-base-multilingual-cased-v2';
const EMBEDDING_DIM = 768;
const SCHEMA_VERSION = 1;

let db: lancedb.Connection | null = null;
let chunksTable: lancedb.Table | null = null;
let summaryTable: lancedb.Table | null = null;
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

  // 初始化 chunks 表
  const tableNames = await db.tableNames();
  if (tableNames.includes('chunks')) {
    chunksTable = await db.openTable('chunks');
    console.log('[VectorDB] opened existing chunks table');
  } else {
    const emptyData = [
      {
        id: '__init__',
        doc_id: '__init__',
        file_name: '__init__',
        chunk_index: 0,
        version: SCHEMA_VERSION,
        content: '__init__',
        vector: new Float32Array(EMBEDDING_DIM),
        metadata: '{}',
      },
    ];
    chunksTable = await db.createTable('chunks', emptyData);
    await chunksTable.delete("id = '__init__'");
    console.log('[VectorDB] created chunks table');
  }

  // 初始化 summary_index 表
  if (tableNames.includes('summary_index')) {
    summaryTable = await db.openTable('summary_index');
    console.log('[VectorDB] opened existing summary_index table');
  } else {
    const emptySummaryData = [
      {
        id: '__init__',
        doc_id: '__init__',
        file_name: '__init__',
        version: SCHEMA_VERSION,
        summary: '__init__',
        chunk_ids: '[]',
        vector: new Float32Array(EMBEDDING_DIM),
        metadata: '{"status":"pending"}',
      },
    ];
    summaryTable = await db.createTable('summary_index', emptySummaryData);
    await summaryTable.delete("id = '__init__'");
    console.log('[VectorDB] created summary_index table');
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
  if (!chunksTable) throw new Error('VectorDB not initialized');

  const rows: any[] = [];
  for (const chunk of chunksArr) {
    const id = `${docId}_${chunk.index}`;
    const embedding = await embed(chunk.content);

    rows.push({
      id,
      doc_id: docId,
      file_name: fileName,
      chunk_index: chunk.index,
      version: SCHEMA_VERSION,
      content: chunk.content,
      vector: new Float32Array(embedding),
      metadata: JSON.stringify({}),
    });
  }

  if (rows.length > 0) {
    await chunksTable.add(rows);
    console.log(`[VectorDB] stored ${rows.length} chunks for ${fileName}`);
  }
}

/**
 * 存储文档摘要到 summary_index 表
 */
export async function storeSummary(params: {
  docId: string;
  fileName: string;
  summary: string;
  chunkIds: string[];
  tags?: string[];
}): Promise<void> {
  if (!summaryTable) throw new Error('VectorDB not initialized');

  // 先删除旧摘要（如果存在）
  await summaryTable.delete(`doc_id = '${params.docId}'`);

  const id = `${params.docId}_summary`;
  const embedding = await embed(params.summary);

  // 规范化标签：trim + 去重 + 限制最多 5 个
  const normalizedTags = params.tags
    ? [...new Set(params.tags.map(t => t.trim()).filter(t => t.length > 0))].slice(0, 5)
    : [];

  await summaryTable.add([{
    id,
    doc_id: params.docId,
    file_name: params.fileName,
    version: SCHEMA_VERSION,
    summary: params.summary,
    chunk_ids: JSON.stringify(params.chunkIds),
    vector: new Float32Array(embedding),
    metadata: JSON.stringify({
      status: 'ready',
      generatedAt: new Date().toISOString(),
      tags: normalizedTags,
    }),
  }]);

  console.log(`[VectorDB] stored summary for ${params.fileName}, tags: ${normalizedTags.join(', ')}`);
}

/**
 * 更新文档标签
 */
export async function updateTags(docId: string, tags: string[]): Promise<void> {
  if (!summaryTable) throw new Error('VectorDB not initialized');

  // 规范化标签
  const normalizedTags = [...new Set(tags.map(t => t.trim()).filter(t => t.length > 0))].slice(0, 5);

  const existing = await summaryTable
    .query()
    .where(`doc_id = '${docId}'`)
    .select(['id', 'metadata'])
    .toArray();

  if (existing.length > 0) {
    const row = existing[0];
    const meta = JSON.parse(row.metadata || '{}');
    meta.tags = normalizedTags;
    await summaryTable.update({
      where: `doc_id = '${docId}'`,
      values: { metadata: JSON.stringify(meta) },
    } as any);
    console.log(`[VectorDB] updated tags for doc ${docId}: ${normalizedTags.join(', ')}`);
  } else {
    console.warn(`[VectorDB] doc ${docId} not found in summary_index`);
  }
}

/**
 * 更新摘要状态（用于标记失败/重试）
 */
export async function updateSummaryStatus(
  docId: string,
  status: 'pending' | 'ready' | 'failed'
): Promise<void> {
  if (!summaryTable) throw new Error('VectorDB not initialized');

  const existing = await summaryTable
    .query()
    .where(`doc_id = '${docId}'`)
    .select(['id', 'metadata'])
    .toArray();

  if (existing.length > 0) {
    const row = existing[0];
    const meta = JSON.parse(row.metadata || '{}');
    meta.status = status;
    await summaryTable.update({
      where: `doc_id = '${docId}'`,
      values: { metadata: JSON.stringify(meta) },
    } as any);
  }
}

/**
 * 仅删除文档摘要（保留 chunks）
 */
export async function deleteSummaryOnly(docId: string): Promise<void> {
  if (!summaryTable) throw new Error('VectorDB not initialized');
  await summaryTable.delete(`doc_id = '${docId}'`);
}

/**
 * 获取文档摘要状态
 */
export async function getSummaryStatus(docId: string): Promise<{
  exists: boolean;
  status?: string;
  summary?: string;
  tags?: string[];
}> {
  if (!summaryTable) throw new Error('VectorDB not initialized');

  const existing = await summaryTable
    .query()
    .where(`doc_id = '${docId}'`)
    .select(['id', 'summary', 'metadata'])
    .toArray();

  if (existing.length === 0) {
    return { exists: false };
  }

  const row = existing[0];
  const meta = JSON.parse(row.metadata || '{}');
  return {
    exists: true,
    status: meta.status || 'pending',
    summary: row.summary,
    tags: meta.tags || [],
  };
}

/**
 * 删除指定文档的所有向量（包括 chunks 和 summary）
 */
export async function deleteChunks(docId: string): Promise<void> {
  if (!chunksTable) throw new Error('VectorDB not initialized');

  await chunksTable.delete(`doc_id = '${docId}'`);
  if (summaryTable) {
    await summaryTable.delete(`doc_id = '${docId}'`);
  }
  console.log(`[VectorDB] deleted chunks and summary for doc: ${docId}`);
}

/**
 * 向量检索（两步检索：summary_index → chunks）
 *
 * 1. 对 summary_index 做向量搜索，召回 50 个候选
 * 2. 若提供 tags，对标签匹配的文档加分（+0.15 相似度）
 * 3. 按 similarity + tagBonus 重新排序，取 topK
 * 4. 通过 chunk_ids 从 chunks 表加载完整原文
 * 5. 降级：summary_index 为空时回退到 chunks 搜索
 */
export async function vectorSearch(
  queryText: string,
  topK: number = 5,
  tags?: string[]
): Promise<any[]> {
  if (!chunksTable) throw new Error('VectorDB not initialized');

  const queryEmbedding = await embed(queryText);
  const queryVector = new Float32Array(queryEmbedding);

  // 检查 summary_index 是否有数据
  let useSummaryFallback = false;
  if (!summaryTable) {
    useSummaryFallback = true;
  } else {
    const summaryCount = await summaryTable.query().select(['id']).toArray();
    if (summaryCount.length === 0) {
      useSummaryFallback = true;
    }
  }

  // 降级模式：直接搜索 chunks
  if (useSummaryFallback) {
    console.log('[VectorDB] summary_index 为空，降级为 chunks 直接搜索');
    const results = await chunksTable
      .vectorSearch(queryVector)
      .limit(topK)
      .select(['id', 'doc_id', 'file_name', 'chunk_index', 'content', 'metadata', 'vector']);

    const rows = await results.toArray();
    return rows.map((row: any) => ({
      chunk: {
        id: row.id,
        docId: row.doc_id,
        fileName: row.file_name,
        chunkIndex: row.chunk_index,
        content: row.content,
        embedding: row.vector ? Array.from(row.vector) : [],
        metadata: JSON.parse(row.metadata || '{}'),
      },
      similarity: row._distance !== undefined ? 1 - row._distance : 0,
      summary: '',
      mode: 'fallback',
    }));
  }

  // 正常模式：summary_index 搜索（召回更多候选，再加权排序）
  // 先召回 50 个摘要用于排序
  const RECALL_TOP_K = 50;

  const summaryResults = await summaryTable
    .vectorSearch(queryVector)
    .limit(RECALL_TOP_K)
    .select(['id', 'doc_id', 'file_name', 'summary', 'chunk_ids', 'metadata']);

  const summaryRows = await summaryResults.toArray();
  if (summaryRows.length === 0) {
    return [];
  }

  // 计算标签加成并重新排序
  const scoredRows = summaryRows.map((row: any) => {
    const baseSimilarity = row._distance !== undefined ? 1 - row._distance : 0;
    let tagBonus = 0;

    if (tags && tags.length > 0) {
      const meta = JSON.parse(row.metadata || '{}');
      const docTags = (meta.tags || []).map((t: string) => t.toLowerCase());
      // 任一标签匹配就加分
      const matchCount = tags.filter(t => docTags.includes(t.toLowerCase())).length;
      if (matchCount > 0) {
        tagBonus = 0.15 * matchCount; // 每个匹配标签 +0.15
      }
    }

    return { ...row, _baseSimilarity: baseSimilarity, _tagBonus: tagBonus, _score: baseSimilarity + tagBonus };
  });

  // 按 _score 降序排序
  scoredRows.sort((a: any, b: any) => b._score - a._score);

  // 取前 topK 个加载完整 chunks
  const topDocs = scoredRows.slice(0, topK);

  // 根据命中的摘要加载完整 chunks
  const fullDocs: any[] = [];
  for (const hit of topDocs) {
    const chunkIds = JSON.parse(hit.chunk_ids || '[]');
    if (chunkIds.length === 0) continue;

    const chunkIdList = chunkIds.map((id: string) => `'${id}'`).join(',');
    const chunkResults = await chunksTable
      .query()
      .where(`id IN (${chunkIdList})`)
      .select(['id', 'doc_id', 'file_name', 'chunk_index', 'content', 'metadata'])
      .toArray();

    // 按 chunk_index 排序
    chunkResults.sort((a: any, b: any) => a.chunk_index - b.chunk_index);

    const meta = JSON.parse(hit.metadata || '{}');
    fullDocs.push({
      docId: hit.doc_id,
      fileName: hit.file_name,
      summary: hit.summary,
      tags: meta.tags || [],
      chunks: chunkResults.map((c: any) => ({
        id: c.id,
        docId: c.doc_id,
        fileName: c.file_name,
        chunkIndex: c.chunk_index,
        content: c.content,
        metadata: JSON.parse(c.metadata || '{}'),
      })),
      similarity: hit._baseSimilarity,
      tagBonus: hit._tagBonus,
      summaryStatus: meta.status || 'ready',
      mode: 'summary',
    });
  }

  // 已经按 _score 排序，不需要再次排序
  return fullDocs;
}

/**
 * 清空所有向量
 */
export async function clearAll(): Promise<void> {
  if (!db) throw new Error('VectorDB not initialized');

  // 清空 chunks 表
  if (chunksTable) {
    const allResults = await chunksTable
      .query()
      .select(['id'])
      .toArray();

    if (allResults.length > 0) {
      const ids = allResults.map((r: any) => `'${r.id}'`).join(',');
      await chunksTable.delete(`id IN (${ids})`);
    }
    console.log('[VectorDB] cleared all chunks');
  }

  // 清空 summary_index 表
  if (summaryTable) {
    const allResults = await summaryTable
      .query()
      .select(['id'])
      .toArray();

    if (allResults.length > 0) {
      const ids = allResults.map((r: any) => `'${r.id}'`).join(',');
      await summaryTable.delete(`id IN (${ids})`);
    }
    console.log('[VectorDB] cleared all summaries');
  }
}

/**
 * 获取统计信息
 */
export async function getStats(): Promise<{ totalChunks: number; totalDocs: number; totalSummaries: number }> {
  if (!chunksTable) throw new Error('VectorDB not initialized');

  const allResults = await chunksTable.query().select(['doc_id']).toArray();
  const totalChunks = allResults.length;
  const docIds = new Set<string>();
  for (const row of allResults) {
    docIds.add(row.doc_id);
  }

  let totalSummaries = 0;
  if (summaryTable) {
    const summaryResults = await summaryTable.query().select(['id']).toArray();
    totalSummaries = summaryResults.length;
  }

  return {
    totalChunks,
    totalDocs: docIds.size,
    totalSummaries,
  };
}

/**
 * 关闭数据库连接
 */
export async function shutdownVectorDB(): Promise<void> {
  // LanceDB 无需显式关闭连接
  chunksTable = null;
  summaryTable = null;
  intentTable = null;
  extractor = null;
  db = null;
  console.log('[VectorDB] shutdown');
}

/**
 * 模型缓存路径（在 knowledge 目录下）
 * Transformers.js 默认缓存结构：cacheDir/<model-id-with-underscores>/onnx/
 */
function getModelCacheDir(knowledgeDir: string): string {
  return join(knowledgeDir, '.model-cache');
}

/**
 * 检测模型是否已缓存
 * 检查关键文件 onnx/model_quantized.onnx 或 onnx/model.onnx 是否存在
 */
function isModelCached(cacheDir: string): boolean {
  // 从 EMBEDDING_MODEL 提取模型目录名（如 'iic/gte_xxx' → 'iic_gte_xxx' 或 'Xenova/xxx' → 'Xenova_xxx'）
  const modelFolder = EMBEDDING_MODEL.replace('/', '_');
  const modelDir = join(cacheDir, modelFolder, 'onnx');
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

// ============================================================
// Intent Vector Store (意图向量)
// ============================================================

let intentTable: lancedb.Table | null = null;

/**
 * 初始化意图向量表
 */
export async function initIntentTable(): Promise<void> {
  if (!db) throw new Error('VectorDB not initialized');

  const tableNames = await db.tableNames();
  if (tableNames.includes('intents')) {
    intentTable = await db.openTable('intents');
    console.log('[VectorDB] opened existing intents table');
  } else {
    const emptyData = [
      {
        id: '__init__',
        text: '__init__',
        vector: new Float32Array(EMBEDDING_DIM),
        ruleId: '__init__',
      },
    ];
    intentTable = await db.createTable('intents', emptyData);
    await intentTable.delete("id = '__init__'");
    console.log('[VectorDB] created intents table');
  }
}

/**
 * 全量索引意图规则
 */
export async function storeIntents(
  rules: { id: string; text: string; ruleId: string }[]
): Promise<void> {
  if (!intentTable) throw new Error('Intent table not initialized');

  // 先清空
  const allResults = await intentTable.query().select(['id']).toArray();
  if (allResults.length > 0) {
    const ids = allResults.map((r: any) => `'${r.id}'`).join(',');
    await intentTable.delete(`id IN (${ids})`);
  }

  if (rules.length === 0) return;

  const rows: any[] = [];
  for (const rule of rules) {
    const embedding = await embed(rule.text);
    rows.push({
      id: rule.id,
      text: rule.text,
      vector: new Float32Array(embedding),
      ruleId: rule.ruleId,
    });
  }

  await intentTable.add(rows);
  console.log(`[VectorDB] stored ${rows.length} intent vectors`);
}

/**
 * 增量更新意图向量
 */
export async function patchIntents(
  toDelete: string[],  // ruleId 列表
  toAdd: { id: string; text: string; ruleId: string }[]
): Promise<void> {
  if (!intentTable) throw new Error('Intent table not initialized');

  // 删除指定 ruleId 的行
  for (const ruleId of toDelete) {
    await intentTable.delete(`ruleId = '${ruleId}'`);
  }

  // 添加新行
  if (toAdd.length > 0) {
    const rows: any[] = [];
    for (const item of toAdd) {
      const embedding = await embed(item.text);
      rows.push({
        id: item.id,
        text: item.text,
        vector: new Float32Array(embedding),
        ruleId: item.ruleId,
      });
    }
    await intentTable.add(rows);
  }
}

/**
 * 意图向量相似度检索
 */
export async function searchIntents(
  queryText: string,
  topK: number = 3
): Promise<Array<{ ruleId: string; score: number }>> {
  if (!intentTable) throw new Error('Intent table not initialized');

  const queryEmbedding = await embed(queryText);
  const queryVector = new Float32Array(queryEmbedding);

  const results = await intentTable
    .vectorSearch(queryVector)
    .limit(topK)
    .select(['id', 'ruleId', 'text']);

  const arr = await results.toArray();
  return arr.map((r: any) => ({
    ruleId: r.ruleId,
    score: 1 - r._distance,
  }));
}
