import { defineStore } from 'pinia'

export interface Strategy {
  id: string
  name: string
  createdAt: string
  updatedAt: string
}

export interface BacktestResult {
  backtestTime: string        // 回测时间 (ISO 字符串)
  features: Record<string, number>  // 策略指标（夏普、收益等）
  summary: {
    buy_count: number
    sell_count: number
    indicator_count?: number
    [key: string]: any
  }
  buy: [symbol: string, timestamp: number, quantity: number, price: number][]
  sell: [symbol: string, timestamp: number, quantity: number, price: number][]
}

export interface Version {
  id: string
  strategyId: string
  saveTime: string   // ISO 字符串
  remark: string
  flowData?: {       // 流程图数据（可选，加载时才需要）
    nodes: any[]
    edges: any[]
  }
  backtestResult?: BacktestResult  // 关联的回测结果
}

// 数据库配置
const DB_NAME = 'QuasarX'
const DB_VERSION = 1
const STORES = {
  STRATEGIES: 'strategies',
  VERSIONS: 'versions',
  FLOW_DATA: 'flow_data',
  BACKTEST_RESULTS: 'backtest_results'
}

// 获取 IndexedDB 连接
function getDB(): Promise<IDBDatabase> {
  return new Promise((resolve, reject) => {
    const request = indexedDB.open(DB_NAME, DB_VERSION)

    request.onerror = () => {
      console.error('[getDB] 打开数据库失败:', request.error)
      reject(request.error)
    }
    request.onsuccess = () => {
      console.info('[getDB] 数据库打开成功')
      resolve(request.result)
    }

    request.onupgradeneeded = (event) => {
      console.info(`[getDB] 数据库升级：版本 ${event.oldVersion} -> ${DB_VERSION}`)
      const db = (event.target as IDBOpenDBRequest).result

      // 创建策略存储
      if (!db.objectStoreNames.contains(STORES.STRATEGIES)) {
        const strategyStore = db.createObjectStore(STORES.STRATEGIES, { keyPath: 'id' })
        strategyStore.createIndex('name', 'name', { unique: false })
        console.info('[getDB] 创建 strategies store')
      }

      // 创建版本存储
      if (!db.objectStoreNames.contains(STORES.VERSIONS)) {
        const versionStore = db.createObjectStore(STORES.VERSIONS, { keyPath: 'id' })
        versionStore.createIndex('strategyId', 'strategyId', { unique: false })
        console.info('[getDB] 创建 versions store')
      }

      // 创建流程图数据存储
      if (!db.objectStoreNames.contains(STORES.FLOW_DATA)) {
        db.createObjectStore(STORES.FLOW_DATA, { keyPath: 'versionId' })
        console.info('[getDB] 创建 flow_data store')
      }

      // 创建回测结果存储
      if (!db.objectStoreNames.contains(STORES.BACKTEST_RESULTS)) {
        db.createObjectStore(STORES.BACKTEST_RESULTS, { keyPath: 'versionId' })
        console.info('[getDB] 创建 backtest_results store')
      }
    }
  })
}

// 通用 CRUD 操作
async function dbGetAll<T>(storeName: string): Promise<T[]> {
  const db = await getDB()
  return new Promise((resolve, reject) => {
    const tx = db.transaction(storeName, 'readonly')
    const store = tx.objectStore(storeName)
    const request = store.getAll()

    request.onerror = () => reject(request.error)
    request.onsuccess = () => resolve(request.result as T[])
  })
}

async function dbGet<T>(storeName: string, key: string): Promise<T | null> {
  const db = await getDB()
  return new Promise((resolve, reject) => {
    const tx = db.transaction(storeName, 'readonly')
    const store = tx.objectStore(storeName)
    const request = store.get(key)

    request.onerror = () => reject(request.error)
    request.onsuccess = () => resolve(request.result as T || null)
  })
}

async function dbPut<T>(storeName: string, value: T): Promise<void> {
  const db = await getDB()
  return new Promise((resolve, reject) => {
    const tx = db.transaction(storeName, 'readwrite')
    const store = tx.objectStore(storeName)
    const request = store.put(value)

    request.onerror = () => {
      console.error(`[dbPut] ${storeName} 保存失败:`, request.error)
      reject(request.error)
    }
    request.onsuccess = () => {
      console.info(`[dbPut] ${storeName} 保存成功`)
      resolve()
    }
  })
}

async function dbDelete(storeName: string, key: string): Promise<void> {
  const db = await getDB()
  return new Promise((resolve, reject) => {
    const tx = db.transaction(storeName, 'readwrite')
    const store = tx.objectStore(storeName)
    const request = store.delete(key)

    request.onerror = () => reject(request.error)
    request.onsuccess = () => resolve()
  })
}

async function dbGetByIndex<T>(storeName: string, indexName: string, value: any): Promise<T[]> {
  const db = await getDB()
  return new Promise((resolve, reject) => {
    const tx = db.transaction(storeName, 'readonly')
    const store = tx.objectStore(storeName)
    const index = store.index(indexName)
    const request = index.getAll(value)

    request.onerror = () => reject(request.error)
    request.onsuccess = () => resolve(request.result as T[])
  })
}

// 辅助函数：生成默认备注（格式化时间）
function formatDate(isoString: string): string {
  const date = new Date(isoString)
  return `${date.getFullYear()}-${String(date.getMonth() + 1).padStart(2, '0')}-${String(date.getDate()).padStart(2, '0')} ${String(date.getHours()).padStart(2, '0')}:${String(date.getMinutes()).padStart(2, '0')}`
}

// 默认示例数据（仅在没有历史数据时使用）
const DEFAULT_STRATEGIES: Strategy[] = []
const DEFAULT_VERSIONS: Version[] = []

export const useHistoryStore = defineStore('history', {
  state: () => ({
    strategies: [] as Strategy[],
    versions: [] as Version[]
  }),

  actions: {
    // 初始化：从 IndexedDB 加载数据
    async initialize() {
      try {
        const [strategies, versions] = await Promise.all([
          dbGetAll<Strategy>(STORES.STRATEGIES),
          dbGetAll<Version>(STORES.VERSIONS)
        ])
        this.strategies = strategies.length > 0 ? strategies : DEFAULT_STRATEGIES
        this.versions = versions.length > 0 ? versions : DEFAULT_VERSIONS
        console.info('[HistoryStore] 数据初始化完成')
      } catch (error) {
        console.error('[HistoryStore] 初始化失败:', error)
        this.strategies = DEFAULT_STRATEGIES
        this.versions = DEFAULT_VERSIONS
      }
    },

    // 持久化策略数据
    async persistStrategies() {
      const tx = (await getDB()).transaction(STORES.STRATEGIES, 'readwrite')
      const store = tx.objectStore(STORES.STRATEGIES)
      store.clear()
      this.strategies.forEach(s => store.put(JSON.parse(JSON.stringify(s))))
    },

    // 持久化版本数据
    async persistVersions() {
      const tx = (await getDB()).transaction(STORES.VERSIONS, 'readwrite')
      const store = tx.objectStore(STORES.VERSIONS)
      store.clear()
      this.versions.forEach(v => store.put(JSON.parse(JSON.stringify(v))))
    },

    // 保存版本的流程图数据
    async saveVersionFlowData(versionId: string, flowData: { nodes: any[], edges: any[] }) {
      console.info(`[saveVersionFlowData] 保存版本 ${versionId} 的流程图数据：${flowData.nodes?.length} 个节点，${flowData.edges?.length} 条边`)
      // 深度克隆以移除响应式引用和不可序列化的属性
      const serializableFlowData = JSON.parse(JSON.stringify(flowData))
      await dbPut(STORES.FLOW_DATA, { versionId, flowData: serializableFlowData })
      console.info(`[saveVersionFlowData] 版本 ${versionId} 的流程图数据已保存`)
    },

    // 加载版本的流程图数据
    async loadVersionFlowData(versionId: string): Promise<{ nodes: any[], edges: any[] } | null> {
      console.info(`[loadVersionFlowData] 加载版本 ${versionId} 的流程图数据`)
      const result = await dbGet<{ versionId: string, flowData: { nodes: any[], edges: any[] } }>(
        STORES.FLOW_DATA,
        versionId
      )
      if (result?.flowData) {
        console.info(`[loadVersionFlowData] 版本 ${versionId} 的流程图数据加载成功：${result.flowData.nodes?.length} 个节点，${result.flowData.edges?.length} 条边`)
      } else {
        console.warn(`[loadVersionFlowData] 版本 ${versionId} 的流程图数据未找到`)
      }
      return result?.flowData || null
    },

    // 更新策略名称
    async updateStrategyName(strategyId: string, newName: string) {
      const strategy = this.strategies.find(s => s.id === strategyId)
      if (strategy) {
        strategy.name = newName
        strategy.updatedAt = new Date().toISOString()
        await dbPut(STORES.STRATEGIES, JSON.parse(JSON.stringify(strategy)))
      }
    },

    // 删除策略及其所有版本
    async removeStrategy(strategyId: string) {
      // 删除策略
      this.strategies = this.strategies.filter(s => s.id !== strategyId)
      await dbDelete(STORES.STRATEGIES, strategyId)

      // 删除关联的版本及其流程数据、回测结果
      const versionsToDelete = this.versions.filter(v => v.strategyId === strategyId)
      this.versions = this.versions.filter(v => v.strategyId !== strategyId)

      for (const version of versionsToDelete) {
        await dbDelete(STORES.VERSIONS, version.id)
        await dbDelete(STORES.FLOW_DATA, version.id)
        await dbDelete(STORES.BACKTEST_RESULTS, version.id)
      }

      await this.persistVersions()
    },

    // 更新版本备注
    async updateVersionRemark(versionId: string, newRemark: string) {
      const version = this.versions.find(v => v.id === versionId)
      if (version) {
        version.remark = newRemark
        await dbPut(STORES.VERSIONS, JSON.parse(JSON.stringify(version)))
      }
    },

    // 删除单个版本
    async removeVersion(versionId: string) {
      const version = this.versions.find(v => v.id === versionId)
      this.versions = this.versions.filter(v => v.id !== versionId)

      await dbDelete(STORES.VERSIONS, versionId)
      await dbDelete(STORES.FLOW_DATA, versionId)
      await dbDelete(STORES.BACKTEST_RESULTS, versionId)
    },

    // 保存回测结果到指定版本
    async saveBacktestResult(versionId: string, backtestResult: BacktestResult) {
      const version = this.versions.find(v => v.id === versionId)
      if (version) {
        version.backtestResult = backtestResult
        await dbPut(STORES.VERSIONS, JSON.parse(JSON.stringify(version)))
        await dbPut(STORES.BACKTEST_RESULTS, JSON.parse(JSON.stringify({ versionId, ...backtestResult })))
      }
    },

    // 加载版本的回测结果
    async loadBacktestResult(versionId: string): Promise<BacktestResult | null> {
      // 优先从内存 versions 中获取
      const version = this.versions.find(v => v.id === versionId)
      if (version?.backtestResult) {
        return version.backtestResult
      }

      // 否则从 IndexedDB 加载
      return await dbGet<BacktestResult>(STORES.BACKTEST_RESULTS, versionId)
    },

    // 新增策略
    async addStrategy(name: string): Promise<string> {
      const now = new Date().toISOString()
      const id = `s${Date.now()}`
      const newStrategy: Strategy = {
        id,
        name,
        createdAt: now,
        updatedAt: now
      }
      this.strategies.push(newStrategy)
      await dbPut(STORES.STRATEGIES, JSON.parse(JSON.stringify(newStrategy)))
      return id
    },

    // 新增版本（保存策略时调用）
    async addVersion(
      strategyId: string,
      flowData?: { nodes: any[], edges: any[] },
      remark?: string
    ): Promise<string> {
      console.info(`[addVersion] 创建新版本，strategyId=${strategyId}, 有 flowData=${!!flowData}`)
      const now = new Date().toISOString()
      const id = `v${Date.now()}`
      const finalRemark = remark ?? formatDate(now)

      const newVersion: Version = {
        id,
        strategyId,
        saveTime: now,
        remark: finalRemark
      }

      this.versions.push(newVersion)
      await dbPut(STORES.VERSIONS, JSON.parse(JSON.stringify(newVersion)))

      // 如果有流程图数据，保存它
      if (flowData) {
        console.info(`[addVersion] 保存流程图数据到版本 ${id}`)
        await this.saveVersionFlowData(id, flowData)
      } else {
        console.warn(`[addVersion] 没有流程图数据提供` )
      }

      return id
    },

    // 创建临时版本（用于临时回测场景）
    async createTempVersion(strategyId: string, flowData: { nodes: any[], edges: any[] }): Promise<string> {
      const now = new Date().toISOString()
      const id = `v${Date.now()}`

      // 深度克隆以移除响应式引用和不可序列化的属性
      const serializableFlowData = JSON.parse(JSON.stringify(flowData))

      const newVersion: Version = {
        id,
        strategyId,
        saveTime: now,
        remark: `临时版本 ${formatDate(now)}`,
        flowData: serializableFlowData
      }

      this.versions.push(newVersion)
      await dbPut(STORES.VERSIONS, JSON.parse(JSON.stringify(newVersion)))
      await this.saveVersionFlowData(id, serializableFlowData)
      return id
    },

    // 将临时版本转换为正式版本（更新备注）
    async convertTempVersion(versionId: string, remark: string): Promise<boolean> {
      const version = this.versions.find(v => v.id === versionId)
      if (version) {
        version.remark = remark
        await dbPut(STORES.VERSIONS, JSON.parse(JSON.stringify(version)))
        return true
      }
      return false
    },

    // 更新策略的 updatedAt
    async touchStrategy(strategyId: string) {
      const strategy = this.strategies.find(s => s.id === strategyId)
      if (strategy) {
        strategy.updatedAt = new Date().toISOString()
        await dbPut(STORES.STRATEGIES, JSON.parse(JSON.stringify(strategy)))
      }
    },

    // 按策略 ID 获取所有版本
    getVersionsByStrategy(strategyId: string): Version[] {
      return this.versions.filter(v => v.strategyId === strategyId)
    },

    // 获取最新版本
    getLatestVersion(strategyId: string): Version | null {
      const versions = this.getVersionsByStrategy(strategyId)
      if (versions.length === 0) return null
      return versions.sort((a, b) =>
        new Date(b.saveTime).getTime() - new Date(a.saveTime).getTime()
      )[0]
    }
  }
})
