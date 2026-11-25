/**
 * LanceDB 记忆存储实现
 *
 * 生产路径：Electron 主进程直接 new（app/electron/memory/ipc.ts 注册 IPC handler）
 * bench 路径：Node 进程里 new（临时目录）
 * 两边用的是同一个类。
 */

import { mkdirSync } from 'fs'
import * as lancedb from '@lancedb/lancedb'

import { MEMORIES_SCHEMA, MEMORIES_TABLE } from './schema'
import { configureEmbeddingEnv, embedText } from './embedder'
import { sqlString, sqlNumber } from './sql'
import type {
  ChannelHits,
  ChannelName,
  FactType,
  MemoryStore,
  MemoryUnit,
  SearchOptions,
} from '../../src/lib/memory/types'

/** 建表参数 */
export interface LanceMemoryStoreOptions {
  /** LanceDB 目录 */
  dir: string
  /** 模型缓存目录（与知识库共用，避免重复下载） */
  cacheDir?: string
  /** 模型下载源 */
  remoteHost?: string
}

/** 索引名（用于 listIndices 判断是否已建） */
const VECTOR_INDEX = 'memories_vector_idx'
const FTS_INDEX = 'memories_text_idx'

/** 记忆单元 → 行 */
function toRow(unit: MemoryUnit, vector: number[]): Record<string, unknown> {
  return {
    id: unit.id,
    bank_id: unit.bankId,
    text: unit.text,
    embed_text: unit.embedText || unit.text,
    vector,
    fact_type: unit.factType,
    occurred_start: unit.occurredStart,
    occurred_end: unit.occurredEnd,
    mentioned_at: unit.mentionedAt,
    confidence: unit.confidence,
    entities: JSON.stringify(unit.entities ?? []),
    source_turn_id: unit.sourceTurnId,
    access_count: unit.accessCount,
    context: unit.context,
  }
}

/** 行 → 记忆单元 */
function fromRow(row: any): MemoryUnit {
  let entities: string[] = []
  try {
    const parsed = JSON.parse(row.entities ?? '[]')
    if (Array.isArray(parsed)) entities = parsed.map(String)
  } catch {
    entities = []
  }
  return {
    id: String(row.id ?? ''),
    bankId: String(row.bank_id ?? ''),
    text: String(row.text ?? ''),
    embedText: String(row.embed_text ?? row.text ?? ''),
    factType: (row.fact_type ?? 'world') as FactType,
    occurredStart: row.occurred_start ?? null,
    occurredEnd: row.occurred_end ?? null,
    mentionedAt: row.mentioned_at ?? 0,
    confidence: row.confidence ?? null,
    entities,
    sourceTurnId: String(row.source_turn_id ?? ''),
    accessCount: row.access_count ?? 0,
    context: String(row.context ?? ''),
  }
}

/** 构造检索条件 */
function buildFilter(opts: SearchOptions): string {
  const parts: string[] = [`bank_id = ${sqlString(opts.bankId)}`]
  if (opts.factTypes && opts.factTypes.length > 0) {
    const list = opts.factTypes.map((f) => sqlString(f)).join(', ')
    parts.push(`fact_type IN (${list})`)
  }
  if (opts.timeRange) {
    parts.push(`(occurred_end IS NULL OR occurred_end >= ${sqlNumber(opts.timeRange.start)})`)
    parts.push(`(occurred_start IS NULL OR occurred_start <= ${sqlNumber(opts.timeRange.end)})`)
  }
  return parts.join(' AND ')
}

/** LanceDB 记忆存储 */
export class LanceMemoryStore implements MemoryStore {
  private db: lancedb.Connection | null = null
  private table: lancedb.Table | null = null
  private readonly opts: LanceMemoryStoreOptions

  constructor(opts: LanceMemoryStoreOptions) {
    this.opts = opts
  }

  /** 连接 + 建表 + 建索引 */
  async init(): Promise<void> {
    if (this.opts.cacheDir || this.opts.remoteHost) {
      configureEmbeddingEnv({
        cacheDir: this.opts.cacheDir,
        remoteHost: this.opts.remoteHost,
      })
    }

    mkdirSync(this.opts.dir, { recursive: true })
    this.db = await lancedb.connect(this.opts.dir)

    const existing = await this.db.tableNames()
    if (existing.includes(MEMORIES_TABLE)) {
      this.table = await this.db.openTable(MEMORIES_TABLE)
    } else {
      this.table = await this.db.createEmptyTable(MEMORIES_TABLE, MEMORIES_SCHEMA)
    }
    await this.ensureIndexes()
  }

  /** 补齐缺失的索引；表为空时跳过向量索引 */
  private async ensureIndexes(): Promise<void> {
    if (!this.table) return
    const indices = await this.table.listIndices()
    const names = new Set(indices.map((i: any) => i.name))

    if (!names.has(FTS_INDEX)) {
      await this.table.createIndex('text', {
        name: FTS_INDEX,
        config: lancedb.Index.fts({ baseTokenizer: 'ngram' }),
      })
    }

    if (!names.has(VECTOR_INDEX)) {
      const rowCount = await this.table.countRows()
      if (rowCount > 0) {
        await this.table.createIndex('vector', {
          name: VECTOR_INDEX,
          config: lancedb.Index.hnswSq({ distanceType: 'cosine' }),
        })
      }
    }
  }

  private requireTable(): lancedb.Table {
    if (!this.table) throw new Error('LanceMemoryStore 未初始化，请先调用 init()')
    return this.table
  }

  /** 写入记忆；同一 (bank, sourceTurnId) 的旧行先删 */
  async upsert(units: MemoryUnit[]): Promise<void> {
    if (!units.length) return
    const table = this.requireTable()

    // 同一 (bank, sourceTurnId) 先删旧行，再写入
    const targets: { bankId: string; sourceTurnId: string }[] = []
    const seenTargets = new Set<string>()
    for (const u of units) {
      if (!u.sourceTurnId) continue
      const key = `${u.bankId}\u0000${u.sourceTurnId}`
      if (seenTargets.has(key)) continue
      seenTargets.add(key)
      targets.push({ bankId: u.bankId, sourceTurnId: u.sourceTurnId })
    }
    for (const t of targets) {
      await table.delete(
        `source_turn_id = ${sqlString(t.sourceTurnId)} AND bank_id = ${sqlString(t.bankId)}`,
      )
    }

    const rows: Record<string, unknown>[] = []
    for (const unit of units) {
      const vector = await embedText(unit.embedText || unit.text)
      rows.push(toRow(unit, vector))
    }
    await table.add(rows)
    await this.ensureIndexes()
  }

  /** 按来源轮次删除 */
  async deleteBySource(sourceTurnId: string, bankId?: string): Promise<number> {
    const table = this.requireTable()
    const before = await table.countRows()
    const scope = bankId ? ` AND bank_id = ${sqlString(bankId)}` : ''
    await table.delete(`source_turn_id = ${sqlString(sourceTurnId)}${scope}`)
    const after = await table.countRows()
    return before - after
  }

  /** 按 bank 删除 */
  async deleteByBank(bankId: string): Promise<number> {
    const table = this.requireTable()
    const before = await table.countRows()
    await table.delete(`bank_id = ${sqlString(bankId)}`)
    const after = await table.countRows()
    return before - after
  }

  /** 各通道独立召回 */
  async searchChannels(opts: SearchOptions): Promise<ChannelHits[]> {
    const table = this.requireTable()
    const where = buildFilter(opts)
    const out: ChannelHits[] = []

    for (const channel of opts.channels) {
      try {
        if (channel === 'vector') {
          const vec = await embedText(opts.query)
          const rows = await table
            .vectorSearch(vec)
            .where(where)
            .limit(opts.limit)
            .select(['id', '_distance'])
            .toArray()
          out.push({
            channel,
            ids: rows.map((r: any) => String(r.id)),
            // 余弦距离 ∈ [0,2]，转成相似度 ∈ [0,1]
            scores: rows.map((r: any) => 1 - Number(r._distance ?? 0)),
          })
        } else if (channel === 'fts') {
          const rows = await table
            .search(opts.query, 'fts')
            .where(where)
            .limit(opts.limit)
            .select(['id', '_score'])
            .toArray()
          out.push({
            channel,
            ids: rows.map((r: any) => String(r.id)),
            scores: rows.map((r: any) => Number(r._score ?? 0)),
          })
        }
        // graph / temporal 通道在对应阶段实现
      } catch (err) {
        console.warn(`[Memory/Store] ${channel} 通道召回失败：`, err)
      }
    }
    return out
  }

  /** 列出某 bank 的全部记忆 */
  async listByBank(bankId: string): Promise<MemoryUnit[]> {
    const table = this.requireTable()
    const rows = await table
      .query()
      .where(`bank_id = ${sqlString(bankId)}`)
      .select([
        'id', 'bank_id', 'text', 'embed_text', 'fact_type',
        'occurred_start', 'occurred_end', 'mentioned_at',
        'confidence', 'entities', 'source_turn_id', 'access_count', 'context',
      ])
      .toArray()
    return rows.map(fromRow)
  }

  /** 按 id 批量取回 */
  async getByIds(ids: string[]): Promise<MemoryUnit[]> {
    if (!ids.length) return []
    const table = this.requireTable()
    const list = ids.map((id) => sqlString(id)).join(', ')
    const rows = await table
      .query()
      .where(`id IN (${list})`)
      .select([
        'id', 'bank_id', 'text', 'embed_text', 'fact_type',
        'occurred_start', 'occurred_end', 'mentioned_at',
        'confidence', 'entities', 'source_turn_id', 'access_count', 'context',
      ])
      .toArray()
    const byId = new Map(rows.map((r: any) => [String(r.id), fromRow(r)]))
    // 按传入顺序返回
    return ids.map((id) => byId.get(id)).filter((u): u is MemoryUnit => !!u)
  }

  /** 关闭连接 */
  async close(): Promise<void> {
    if (this.db) {
      this.db.close()
      this.db = null
      this.table = null
    }
  }
}

/** 通道名白名单（供上层校验） */
export const SUPPORTED_CHANNELS: ChannelName[] = ['vector', 'fts']
