/**
 * 记忆表 Arrow Schema —— 单一事实源
 *
 * 对应记忆单元 f = (u, b, t, v, τs, τe, τm, ℓ, c, x)：
 *
 * | 列             | 符号 | 说明                                |
 * |----------------|------|-------------------------------------|
 * | id             | u    | 记忆单元 id                         |
 * | bank_id        | b    | bank 作用域                         |
 * | text           | t    | 叙事事实正文（展示 + 全文检索）      |
 * | embed_text     |      | 嵌入用文本 = text + 人类可读时间前缀 |
 * | vector         | v    | 768 维归一化向量                    |
 * | fact_type      | ℓ    | world / experience / opinion / observation |
 * | occurred_start | τs   | 发生区间起点（毫秒时间戳）           |
 * | occurred_end   | τe   | 发生区间终点（毫秒时间戳）           |
 * | mentioned_at   | τm   | 记录时间（毫秒时间戳）               |
 * | confidence     | c    | 置信度 ∈ [0,1]，仅 opinion 用        |
 * | entities       |      | 实体数组（JSON 字符串）              |
 * | source_turn_id |      | 幂等键                               |
 * | access_count   | x    | 访问计数                            |
 * | context        | x    | 上下文                              |
 *
 * LanceDB 的 schema 即 Arrow schema，改列需重建表。
 * 字段一律 nullable：追加行时缺失的列补 null，而不是补默认值。
 *
 * 时间戳用 Float64 而非 Int64：毫秒时间戳落在 double 的精确整数区间内，
 * 且 Int64 在 apache-arrow JS 中读出为 BigInt，会影响 IPC 与 JSON 序列化。
 */

import { Schema, Field, FixedSizeList, Float32, Float64, Utf8, Int32 } from 'apache-arrow'

/** 嵌入模型名 —— 与 electron/vectorDB.ts 保持一致 */
export const EMBEDDING_MODEL = 'Xenova/distiluse-base-multilingual-cased-v2'

/** 嵌入维度 —— 建表时固定 */
export const EMBEDDING_DIM = 768

/** 表名 */
export const MEMORIES_TABLE = 'memories'

/** memories 表的 Arrow Schema */
export const MEMORIES_SCHEMA = new Schema([
  new Field('id', new Utf8(), true),
  new Field('bank_id', new Utf8(), true),
  new Field('text', new Utf8(), true),
  new Field('embed_text', new Utf8(), true),
  new Field('vector', new FixedSizeList(EMBEDDING_DIM, new Field('item', new Float32())), true),
  new Field('fact_type', new Utf8(), true),
  new Field('occurred_start', new Float64(), true),
  new Field('occurred_end', new Float64(), true),
  new Field('mentioned_at', new Float64(), true),
  new Field('confidence', new Float64(), true),
  new Field('entities', new Utf8(), true),
  new Field('source_turn_id', new Utf8(), true),
  new Field('access_count', new Int32(), true),
  new Field('context', new Utf8(), true),
])

/** 表列名 */
export const MEMORY_COLUMNS = MEMORIES_SCHEMA.fields.map((f) => f.name)
