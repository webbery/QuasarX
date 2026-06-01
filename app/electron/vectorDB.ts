/**
 * 向量数据库（主进程）
 * 使用 LanceDB 存储 + @xenova/transformers 生成 embedding
 */

import { mkdirSync, existsSync, rmSync } from 'fs';
import { join } from 'path';
import * as lancedb from '@lancedb/lancedb';
import { pipeline, env } from '@xenova/transformers';
import { Schema, Field, FixedSizeList, Float32, Utf8, Int32 } from 'apache-arrow';

const EMBEDDING_MODEL = 'Xenova/distiluse-base-multilingual-cased-v2';
const EMBEDDING_DIM = 768;
const SCHEMA_VERSION = 1;

let db: lancedb.Connection | null = null;
let chunksTable: lancedb.Table | null = null;
let chunkSummariesTable: lancedb.Table | null = null;
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
    // ★ 使用 Arrow Schema 明确定义 vector 列为 FixedSizeList(Float32)
    const chunksSchema = new Schema([
      new Field('id', new Utf8(), true),
      new Field('doc_id', new Utf8(), true),
      new Field('file_name', new Utf8(), true),
      new Field('chunk_index', new Int32(), true),
      new Field('version', new Int32(), true),
      new Field('content', new Utf8(), true),
      new Field('vector', new FixedSizeList(EMBEDDING_DIM, new Field('item', new Float32())), true),
      new Field('metadata', new Utf8(), true),
    ]);

    const emptyData = [
      {
        id: '__init__',
        doc_id: '__init__',
        file_name: '__init__',
        chunk_index: 0,
        version: SCHEMA_VERSION,
        content: '__init__',
        vector: Array(EMBEDDING_DIM).fill(0),
        metadata: '{}',
      },
    ];
    chunksTable = await db.createTable('chunks', emptyData, { schema: chunksSchema });
    await chunksTable.delete("id = '__init__'");
    console.log('[VectorDB] created chunks table with Arrow schema');
  }

  // 初始化 summary_index 表
  if (tableNames.includes('summary_index')) {
    summaryTable = await db.openTable('summary_index');
    console.log('[VectorDB] opened existing summary_index table');
  } else {
    // ★ 使用 Arrow Schema 明确定义 vector 列
    const summarySchema = new Schema([
      new Field('id', new Utf8(), true),
      new Field('doc_id', new Utf8(), true),
      new Field('file_name', new Utf8(), true),
      new Field('version', new Int32(), true),
      new Field('summary', new Utf8(), true),
      new Field('chunk_ids', new Utf8(), true),
      new Field('vector', new FixedSizeList(EMBEDDING_DIM, new Field('item', new Float32())), true),
      new Field('metadata', new Utf8(), true),
    ]);

    const emptySummaryData = [
      {
        id: '__init__',
        doc_id: '__init__',
        file_name: '__init__',
        version: SCHEMA_VERSION,
        summary: '__init__',
        chunk_ids: '[]',
        vector: Array(EMBEDDING_DIM).fill(0),
        metadata: '{"status":"pending"}',
      },
    ];
    summaryTable = await db.createTable('summary_index', emptySummaryData, { schema: summarySchema });
    await summaryTable.delete("id = '__init__'");
    console.log('[VectorDB] created summary_index table with Arrow schema');
  }

  // 初始化 chunk_summaries 表（段落意义总结）
  if (tableNames.includes('chunk_summaries')) {
    chunkSummariesTable = await db.openTable('chunk_summaries');
    console.log('[VectorDB] opened existing chunk_summaries table');
  } else {
    const chunkSummariesSchema = new Schema([
      new Field('id', new Utf8(), true),
      new Field('doc_id', new Utf8(), true),
      new Field('file_name', new Utf8(), true),
      new Field('chunk_id', new Utf8(), true),
      new Field('chunk_index', new Int32(), true),
      new Field('version', new Int32(), true),
      new Field('summary', new Utf8(), true),
      new Field('vector', new FixedSizeList(EMBEDDING_DIM, new Field('item', new Float32())), true),
    ]);

    const emptyChunkSummariesData = [
      {
        id: '__init__',
        doc_id: '__init__',
        file_name: '__init__',
        chunk_id: '__init__',
        chunk_index: 0,
        version: SCHEMA_VERSION,
        summary: '__init__',
        vector: Array(EMBEDDING_DIM).fill(0),
      },
    ];
    chunkSummariesTable = await db.createTable('chunk_summaries', emptyChunkSummariesData, { schema: chunkSummariesSchema });
    await chunkSummariesTable.delete("id = '__init__'");
    console.log('[VectorDB] created chunk_summaries table with Arrow schema');
  }
}

/**
 * 存储文档的文本块
 */
export async function storeChunks(
  docId: string,
  fileName: string,
  chunksArr: { index: number; content: string }[],
  pages?: number  // ★ 新增：文件页数
): Promise<void> {
  if (!chunksTable) throw new Error('VectorDB not initialized');

  console.log(`[VectorDB] storeChunks called: docId=${docId}, fileName=${fileName}, chunks=${chunksArr.length}, pages=${pages || 'unknown'}`);

  const rows: any[] = [];
  for (const chunk of chunksArr) {
    const id = `${docId}_${chunk.index}`;
    const embedding = await embed(chunk.content);

    // 调试日志：记录向量生成
    console.log(`[VectorDB] chunk ${id}: embedding dimension=${embedding.length}, first3=[${embedding.slice(0, 3).join(',')}]`);

    const row = {
      id,
      doc_id: docId,
      file_name: fileName,
      chunk_index: chunk.index,
      version: SCHEMA_VERSION,
      content: chunk.content,
      vector: embedding,  // ★ 使用 number[] 而非 Float32Array，LanceDB 会自动转换为 FixedSizeList
      metadata: JSON.stringify({ pages: pages || 0 }),  // ★ 保存页数到 metadata
    };

    // 调试日志：验证向量类型
    console.log(`[VectorDB] chunk ${id}: vector type=${row.vector.constructor.name}, length=${row.vector.length}, pages=${pages}`);

    rows.push(row);
  }

  if (rows.length > 0) {
    console.log(`[VectorDB] adding ${rows.length} rows to chunks table...`);
    await chunksTable.add(rows);
    console.log(`[VectorDB] ✓ stored ${rows.length} chunks for ${fileName}`);

    // 调试日志：验证写入结果
    const countResult = await chunksTable.query().where(`doc_id = '${docId}'`).select(['id']).toArray();
    console.log(`[VectorDB] verification: chunks table now has ${countResult.length} rows for docId=${docId}`);
  }
}

/**
 * 存储 chunk 的段落意义总结到 chunk_summaries 表
 */
export async function storeChunkSummaries(
  docId: string,
  fileName: string,
  chunkSummaries: { chunkId: string; chunkIndex: number; summary: string }[]
): Promise<void> {
  if (!chunkSummariesTable) throw new Error('VectorDB not initialized');

  console.log(`[VectorDB] storeChunkSummaries called: docId=${docId}, fileName=${fileName}, summaries=${chunkSummaries.length}`);

  // 先删除旧的 chunk summaries（如果存在）
  await chunkSummariesTable.delete(`doc_id = '${docId}'`);

  const rows: any[] = [];
  for (const cs of chunkSummaries) {
    const id = `${docId}_summary_${cs.chunkIndex}`;
    const embedding = await embed(cs.summary);

    console.log(`[VectorDB] chunk_summary ${id}: embedding dimension=${embedding.length}`);

    const row = {
      id,
      doc_id: docId,
      file_name: fileName,
      chunk_id: cs.chunkId,
      chunk_index: cs.chunkIndex,
      version: SCHEMA_VERSION,
      summary: cs.summary,
      vector: embedding,
    };

    rows.push(row);
  }

  if (rows.length > 0) {
    console.log(`[VectorDB] adding ${rows.length} rows to chunk_summaries table...`);
    await chunkSummariesTable.add(rows);
    console.log(`[VectorDB] ✓ stored ${rows.length} chunk summaries for ${fileName}`);
  }
}

/**
 * 获取指定文档的所有 chunk summaries
 */
export async function getChunkSummariesByDocId(docId: string): Promise<any[]> {
  if (!chunkSummariesTable) throw new Error('VectorDB not initialized');

  const results = await chunkSummariesTable
    .query()
    .where(`doc_id = '${docId}'`)
    .select(['id', 'chunk_id', 'chunk_index', 'summary'])
    .toArray();

  // 按 chunk_index 排序
  return results
    .map((row: any) => ({
      id: row.id,
      chunk_id: row.chunk_id,
      chunk_index: row.chunk_index,
      summary: row.summary,
    }))
    .sort((a: any, b: any) => a.chunk_index - b.chunk_index);
}

/**
 * 获取指定文档的所有 chunks
 */
export async function getChunksByDocId(docId: string): Promise<any[]> {
  if (!chunksTable) throw new Error('VectorDB not initialized');

  const results = await chunksTable
    .query()
    .where(`doc_id = '${docId}'`)
    .select(['id', 'content', 'chunk_index'])
    .toArray();

  // 按 chunk_index 排序
  return results
    .map((row: any) => ({
      id: row.id,
      content: row.content,
      chunk_index: row.chunk_index,
    }))
    .sort((a: any, b: any) => a.chunk_index - b.chunk_index);
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
  pages?: number;  // ★ 新增：页数信息
}): Promise<void> {
  if (!summaryTable) throw new Error('VectorDB not initialized');

  console.log(`[VectorDB] storeSummary called: docId=${params.docId}, fileName=${params.fileName}, pages=${params.pages || 'unknown'}`);
  console.log(`[VectorDB] summary length=${params.summary.length}, chunkIds count=${params.chunkIds.length}`);

  // 先删除旧摘要（如果存在）
  console.log(`[VectorDB] deleting old summary for docId=${params.docId}...`);
  await summaryTable.delete(`doc_id = '${params.docId}'`);

  const id = `${params.docId}_summary`;
  const embedding = await embed(params.summary);

  // 调试日志：记录摘要向量生成
  console.log(`[VectorDB] summary ${id}: embedding dimension=${embedding.length}, first3=[${embedding.slice(0, 3).join(',')}]`);

  // 规范化标签：trim + 去重 + 限制最多 5 个
  const normalizedTags = params.tags
    ? [...new Set(params.tags.map(t => t.trim()).filter(t => t.length > 0))].slice(0, 5)
    : [];

  const summaryRow = {
    id,
    doc_id: params.docId,
    file_name: params.fileName,
    version: SCHEMA_VERSION,
    summary: params.summary,
    chunk_ids: JSON.stringify(params.chunkIds),
    vector: embedding,  // ★ 使用 number[] 而非 Float32Array
    metadata: JSON.stringify({
      status: 'ready',
      generatedAt: new Date().toISOString(),
      tags: normalizedTags,
      pages: params.pages || 0,  // ★ 保存页数到 summary metadata
    }),
  };

  // 调试日志：验证向量类型
  console.log(`[VectorDB] summary ${id}: vector type=${summaryRow.vector.constructor.name}, length=${summaryRow.vector.length}`);
  console.log(`[VectorDB] summary ${id}: tags=[${normalizedTags.join(', ')}], pages=${params.pages}`);

  console.log(`[VectorDB] adding summary row to summary_index table...`);
  await summaryTable.add([summaryRow]);

  console.log(`[VectorDB] ✓ stored summary for ${params.fileName}, tags: ${normalizedTags.join(', ')}, pages: ${params.pages || 0}`);

  // 调试日志：验证写入结果
  const verifyResult = await summaryTable.query().where(`doc_id = '${params.docId}'`).select(['id', 'metadata']).toArray();
  if (verifyResult.length > 0) {
    const meta = JSON.parse(verifyResult[0].metadata || '{}');
    console.log(`[VectorDB] verification: summary exists, status=${meta.status || 'unknown'}, pages=${meta.pages || 0}`);
  } else {
    console.warn(`[VectorDB] ⚠ verification failed: summary not found after insert!`);
  }
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
  pages?: number;
  chunkCount?: number;
}> {
  if (!summaryTable) throw new Error('VectorDB not initialized');

  const existing = await summaryTable
    .query()
    .where(`doc_id = '${docId}'`)
    .select(['id', 'summary', 'metadata'])
    .toArray();

  // 查询 chunks 数量（无论 summary 是否存在都查询）
  let chunkCount = 0;
  if (chunksTable) {
    try {
      const chunkResults = await chunksTable
        .query()
        .where(`doc_id = '${docId}'`)
        .select(['id'])
        .toArray();
      chunkCount = chunkResults.length;
      console.log(`[getSummaryStatus] doc ${docId}: chunkCount = ${chunkCount}`);
    } catch (e) {
      console.warn(`[getSummaryStatus] failed to count chunks for ${docId}:`, e);
    }
  }

  if (existing.length === 0) {
    // summary_index 中不存在，但可能有 chunks
    return { exists: false, chunkCount };
  }

  const row = existing[0];
  const meta = JSON.parse(row.metadata || '{}');

  return {
    exists: true,
    status: meta.status || 'pending',
    summary: row.summary,
    tags: meta.tags || [],
    pages: meta.pages || 0,
    chunkCount,
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
 * 向量检索（新方案：chunk_summaries → chunks）
 *
 * 1. 对 chunk_summaries 做向量搜索，召回 50 个候选
 * 2. 若提供 tags，从 summary_index 获取文档标签并加分（+0.15 相似度）
 * 3. 按 similarity + tagBonus 重新排序，取 topK
 * 4. 通过 chunk_id 从 chunks 表加载完整原文
 * 5. 降级：chunk_summaries 为空时回退到 chunks 搜索
 */
export async function vectorSearch(
  queryText: string,
  topK: number = 5,
  tags?: string[]
): Promise<any[]> {
  if (!chunksTable) throw new Error('VectorDB not initialized');

  console.log(`[VectorDB] vectorSearch called: query="${queryText}", topK=${topK}, tags=[${tags?.join(', ') || 'none'}]`);

  const queryEmbedding = await embed(queryText);
  const queryVector = new Float32Array(queryEmbedding);

  console.log(`[VectorDB] query embedding dimension=${queryEmbedding.length}, first3=[${queryEmbedding.slice(0, 3).join(',')}]`);

  // 检查 chunk_summaries 是否有数据
  let useFallback = false;
  if (!chunkSummariesTable) {
    console.log('[VectorDB] chunkSummariesTable is null, using fallback');
    useFallback = true;
  } else {
    const chunkSummariesCount = await chunkSummariesTable.query().select(['id']).toArray();
    console.log(`[VectorDB] chunk_summaries has ${chunkSummariesCount.length} rows`);
    if (chunkSummariesCount.length === 0) {
      console.log('[VectorDB] chunk_summaries is empty, using fallback');
      useFallback = true;
    }
  }

  // 降级模式：直接搜索 chunks
  if (useFallback) {
    console.log('[VectorDB] ⚠ fallback mode: searching chunks directly');
    try {
      const results = await chunksTable
        .vectorSearch(queryVector)
        .column('vector')
        .limit(topK)
        .select(['id', 'doc_id', 'file_name', 'chunk_index', 'content', 'metadata', 'vector']);

      const rows = await results.toArray();
      console.log(`[VectorDB] fallback search returned ${rows.length} results`);
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
    } catch (error: any) {
      console.error(`[VectorDB] ❌ fallback search failed:`, error);
      throw error;
    }
  }

  // 正常模式：chunk_summaries 搜索
  const RECALL_TOP_K = 50;

  console.log(`[VectorDB] normal mode: searching chunk_summaries with recall=${RECALL_TOP_K}`);

  try {
    const chunkSummariesResults = await chunkSummariesTable
      .vectorSearch(queryVector)
      .column('vector')
      .limit(RECALL_TOP_K)
      .select(['id', 'doc_id', 'file_name', 'chunk_id', 'chunk_index', 'summary']);

    const chunkSummariesRows = await chunkSummariesResults.toArray();
    console.log(`[VectorDB] chunk_summaries search returned ${chunkSummariesRows.length} results`);

    if (chunkSummariesRows.length === 0) {
      console.log('[VectorDB] no results from chunk_summaries search, returning empty');
      return [];
    }

    // 如果需要标签加成，从 summary_index 获取文档标签
    const docTagsMap = new Map<string, string[]>();
    if (tags && tags.length > 0) {
      const summaryResults = await summaryTable
        ?.query()
        .select(['doc_id', 'metadata'])
        .toArray();

      if (summaryResults) {
        for (const row of summaryResults) {
          const meta = JSON.parse(row.metadata || '{}');
          if (meta.tags && meta.tags.length > 0) {
            docTagsMap.set(row.doc_id, meta.tags);
          }
        }
      }
    }

    // 计算标签加成并重新排序
    const scoredRows = chunkSummariesRows.map((row: any) => {
      const baseSimilarity = row._distance !== undefined ? 1 - row._distance : 0;
      let tagBonus = 0;

      if (tags && tags.length > 0) {
        const docTags = docTagsMap.get(row.doc_id) || [];
        const docTagsLower = docTags.map((t: string) => t.toLowerCase());
        const matchCount = tags.filter(t => docTagsLower.includes(t.toLowerCase())).length;
        if (matchCount > 0) {
          tagBonus = 0.15 * matchCount;
          console.log(`[VectorDB] tag bonus: ${matchCount} tags matched for ${row.doc_id}, bonus=${tagBonus}`);
        }
      }

      return { ...row, _baseSimilarity: baseSimilarity, _tagBonus: tagBonus, _score: baseSimilarity + tagBonus };
    });

    // 按 _score 降序排序
    scoredRows.sort((a: any, b: any) => b._score - a._score);

    // 按 doc_id 分组，取前 topK 个文档
    const docGroups = new Map<string, any[]>();
    for (const row of scoredRows) {
      if (!docGroups.has(row.doc_id)) {
        docGroups.set(row.doc_id, []);
      }
      docGroups.get(row.doc_id)!.push(row);
    }

    // 取前 topK 个文档
    const topDocs = Array.from(docGroups.entries())
      .slice(0, topK)
      .map(([docId, chunks]) => ({
        docId,
        fileName: chunks[0].file_name,
        chunks,
        similarity: chunks[0]._baseSimilarity,
        tagBonus: chunks[0]._tagBonus,
      }));

    console.log(`[VectorDB] top ${topDocs.length} docs after grouping and slicing`);

    // 加载完整 chunks 原文
    const fullDocs: any[] = [];
    for (const hit of topDocs) {
      const chunkIdList = hit.chunks.map((c: any) => `'${c.chunk_id}'`).join(',');
      const chunkResults = await chunksTable
        .query()
        .where(`id IN (${chunkIdList})`)
        .select(['id', 'doc_id', 'file_name', 'chunk_index', 'content', 'metadata'])
        .toArray();

      // 按 chunk_index 排序
      chunkResults.sort((a: any, b: any) => a.chunk_index - b.chunk_index);

      // 获取文档级摘要和标签
      const summaryResult = await summaryTable
        ?.query()
        .where(`doc_id = '${hit.docId}'`)
        .select(['summary', 'metadata'])
        .toArray();

      let summary = '';
      let docTags: string[] = [];
      if (summaryResult && summaryResult.length > 0) {
        summary = summaryResult[0].summary || '';
        const meta = JSON.parse(summaryResult[0].metadata || '{}');
        docTags = meta.tags || [];
      }

      fullDocs.push({
        docId: hit.docId,
        fileName: hit.fileName,
        summary,
        tags: docTags,
        chunks: chunkResults.map((c: any) => ({
          id: c.id,
          docId: c.doc_id,
          fileName: c.file_name,
          chunkIndex: c.chunk_index,
          content: c.content,
          metadata: JSON.parse(c.metadata || '{}'),
        })),
        similarity: hit.similarity,
        tagBonus: hit.tagBonus,
        mode: 'chunk_summaries',
      });
    }

    console.log(`[VectorDB] ✓ vectorSearch returning ${fullDocs.length} docs`);
    return fullDocs;
  } catch (error: any) {
    console.error(`[VectorDB] ❌ chunk_summaries search failed:`, error);
    throw error;
  }
}

/**
 * 获取指定文档的页数（从 chunks 表的 metadata 中读取）
 */
export async function getPages(docId: string): Promise<number> {
  if (!chunksTable) throw new Error('VectorDB not initialized');

  const results = await chunksTable
    .query()
    .where(`doc_id = '${docId}'`)
    .select(['metadata'])
    .limit(1)
    .toArray();

  if (results.length > 0) {
    try {
      const metadata = JSON.parse(results[0].metadata || '{}');
      return metadata.pages || 0;
    } catch {
      return 0;
    }
  }
  return 0;
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
    // ★ 使用 Arrow Schema 定义 intents 表
    const intentsSchema = new Schema([
      new Field('id', new Utf8(), true),
      new Field('text', new Utf8(), true),
      new Field('vector', new FixedSizeList(EMBEDDING_DIM, new Field('item', new Float32())), true),
      new Field('ruleId', new Utf8(), true),
    ]);

    const emptyData = [
      {
        id: '__init__',
        text: '__init__',
        vector: Array(EMBEDDING_DIM).fill(0),
        ruleId: '__init__',
      },
    ];
    intentTable = await db.createTable('intents', emptyData, { schema: intentsSchema });
    await intentTable.delete("id = '__init__'");
    console.log('[VectorDB] created intents table with Arrow schema');
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
      vector: embedding,  // ★ 使用 number[] 而非 Float32Array
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
        vector: embedding,  // ★ 使用 number[] 而非 Float32Array
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
    .column('vector')
    .limit(topK)
    .select(['id', 'ruleId', 'text']);

  const arr = await results.toArray();
  return arr.map((r: any) => ({
    ruleId: r.ruleId,
    score: 1 - r._distance,
  }));
}
