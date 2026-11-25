/**
 * 记忆模块的领域模型与存储端口
 *
 * 这是记忆模块对外的唯一契约文件：
 * - `src/lib/agent/**` 只允许 import 本文件，不得 import 适配器或存储实现
 * - 适配器用技术语言，端口用领域语言 —— 换后端只改适配器
 */

/** 四个记忆网络 */
export type FactType = 'world' | 'experience' | 'opinion' | 'observation'

/**
 * 一条记忆单元
 *
 * 对应 f = (u, b, t, v, τs, τe, τm, ℓ, c, x)
 */
export interface MemoryUnit {
  /** u —— 记忆单元 id */
  id: string
  /** b —— bank 作用域 */
  bankId: string
  /** t —— 叙事事实正文（展示 + 全文检索） */
  text: string
  /** 嵌入用文本 = text + 人类可读时间前缀 */
  embedText: string
  /** ℓ —— 事实类型 */
  factType: FactType
  /** τs —— 发生区间起点（毫秒时间戳） */
  occurredStart: number | null
  /** τe —— 发生区间终点（毫秒时间戳） */
  occurredEnd: number | null
  /** τm —— 记录时间（毫秒时间戳） */
  mentionedAt: number
  /** c —— 置信度 ∈ [0,1]，仅 opinion 使用 */
  confidence: number | null
  /** 涉及的实体 */
  entities: string[]
  /** 幂等键：同一 id 重复写入先删旧再灌 */
  sourceTurnId: string
  /** x —— 访问计数 */
  accessCount: number
  /** x —— 上下文 */
  context: string
}

/** 检索通道 */
export type ChannelName = 'vector' | 'fts' | 'graph' | 'temporal'

/** 检索请求 */
export interface SearchOptions {
  /** bank 作用域 */
  bankId: string
  /** 查询串 */
  query: string
  /** 每个通道的召回条数 */
  limit: number
  /** 限定事实类型 */
  factTypes?: FactType[]
  /** 时间区间过滤 */
  timeRange?: { start: number; end: number }
  /** 参与的通道 */
  channels: ChannelName[]
}

/**
 * 单个通道的召回结果
 *
 * `ids` 是按分数降序的 id 列表 —— 跨通道融合（RRF）只吃 rank。
 * `scores` 与 `ids` 一一对应，存该通道的原生分数（余弦 / BM25），
 * 供通道内加权（如时间衰减）使用；不同通道之间不可直接比较。
 */
export interface ChannelHits {
  /** 通道名 */
  channel: ChannelName
  /** 按分数降序的 id 列表 */
  ids: string[]
  /** 与 ids 一一对应的通道原生分数 */
  scores?: number[]
}

/**
 * 记忆存储端口
 *
 * 实现方：Electron 主进程 LanceDB（生产）/ 浏览器 IndexedDB（Agent Lab）/ Node 临时目录（bench）
 */
export interface MemoryStore {
  /** 建表 / 建索引 */
  init(): Promise<void>
  /** 写入记忆（按 sourceTurnId 幂等） */
  upsert(units: MemoryUnit[]): Promise<void>
  /** 按来源轮次删除，返回删除条数；给 bankId 时只删该 bank 内的 */
  deleteBySource(sourceTurnId: string, bankId?: string): Promise<number>
  /** 按 bank 删除，返回删除条数 */
  deleteByBank(bankId: string): Promise<number>
  /** 各检索通道独立召回，融合交给上层 */
  searchChannels(opts: SearchOptions): Promise<ChannelHits[]>
  /** 列出某 bank 的全部记忆 */
  listByBank(bankId: string): Promise<MemoryUnit[]>
  /** 按 id 批量取回 */
  getByIds(ids: string[]): Promise<MemoryUnit[]>
  /** 关闭连接 */
  close(): Promise<void>
}
