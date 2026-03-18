import { defineStore } from 'pinia'

export interface Strategy {
  id: string
  name: string
  createdAt: string
  updatedAt: string
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
}

// 辅助函数：生成默认备注（格式化时间）
function formatDate(isoString: string): string {
  const date = new Date(isoString)
  return `${date.getFullYear()}-${String(date.getMonth() + 1).padStart(2, '0')}-${String(date.getDate()).padStart(2, '0')} ${String(date.getHours()).padStart(2, '0')}:${String(date.getMinutes()).padStart(2, '0')}`
}

// localStorage 键名
const STORAGE_KEYS = {
  STRATEGIES: 'quasarx_strategies',
  VERSIONS: 'quasarx_versions',
  VERSION_FLOW_DATA: 'quasarx_version_flow_' // 前缀 + versionId
}

// 从 localStorage 加载数据
function loadFromStorage<T>(key: string, defaultValue: T): T {
  try {
    const item = localStorage.getItem(key)
    if (item) {
      return JSON.parse(item) as T
    }
  } catch (error) {
    console.error(`从 localStorage 加载 ${key} 失败:`, error)
  }
  return defaultValue
}

// 保存数据到 localStorage
function saveToStorage<T>(key: string, value: T): void {
  try {
    localStorage.setItem(key, JSON.stringify(value))
  } catch (error) {
    console.error(`保存到 localStorage ${key} 失败:`, error)
  }
}

// 默认示例数据（仅在没有历史数据时使用）
const DEFAULT_STRATEGIES: Strategy[] = [
  // { id: 's1', name: '策略 A', createdAt: new Date('2023-01-01').toISOString(), updatedAt: new Date('2023-01-01').toISOString() },
  // { id: 's2', name: '策略 B', createdAt: new Date('2023-02-01').toISOString(), updatedAt: new Date('2023-02-01').toISOString() },
  // { id: 's3', name: '策略 C', createdAt: new Date('2023-03-01').toISOString(), updatedAt: new Date('2023-03-01').toISOString() }
]

const DEFAULT_VERSIONS: Version[] = [
  // { id: 'v1', strategyId: 's1', saveTime: '2023-01-01T12:00:00', remark: formatDate('2023-01-01T12:00:00') },
  // { id: 'v2', strategyId: 's1', saveTime: '2023-02-01T15:30:00', remark: '重要版本' },
  // { id: 'v3', strategyId: 's2', saveTime: '2023-03-01T09:20:00', remark: formatDate('2023-03-01T09:20:00') },
  // { id: 'v4', strategyId: 's3', saveTime: '2023-04-01T18:45:00', remark: formatDate('2023-04-01T18:45:00') },
  // { id: 'v5', strategyId: 's3', saveTime: '2023-05-01T08:10:00', remark: '测试版本' },
  // { id: 'v6', strategyId: 's3', saveTime: '2023-06-01T22:30:00', remark: formatDate('2023-06-01T22:30:00') }
]

export const useHistoryStore = defineStore('history', {
  state: () => ({
    strategies: loadFromStorage<Strategy[]>(STORAGE_KEYS.STRATEGIES, DEFAULT_STRATEGIES),
    versions: loadFromStorage<Version[]>(STORAGE_KEYS.VERSIONS, DEFAULT_VERSIONS)
  }),

  actions: {
    // 持久化策略数据
    persistStrategies() {
      saveToStorage(STORAGE_KEYS.STRATEGIES, this.strategies)
    },

    // 持久化版本数据
    persistVersions() {
      saveToStorage(STORAGE_KEYS.VERSIONS, this.versions)
    },

    // 保存版本的流程图数据
    saveVersionFlowData(versionId: string, flowData: { nodes: any[], edges: any[] }) {
      const key = `${STORAGE_KEYS.VERSION_FLOW_DATA}${versionId}`
      saveToStorage(key, flowData)
    },

    // 加载版本的流程图数据
    loadVersionFlowData(versionId: string): { nodes: any[], edges: any[] } | null {
      const key = `${STORAGE_KEYS.VERSION_FLOW_DATA}${versionId}`
      return loadFromStorage<{ nodes: any[], edges: any[] } | null>(key, null)
    },

    // 更新策略名称
    updateStrategyName(strategyId: string, newName: string) {
      const strategy = this.strategies.find(s => s.id === strategyId)
      if (strategy) {
        strategy.name = newName
        strategy.updatedAt = new Date().toISOString()
        this.persistStrategies()
      }
    },

    // 删除策略及其所有版本
    removeStrategy(strategyId: string) {
      // 删除策略
      this.strategies = this.strategies.filter(s => s.id !== strategyId)
      // 删除关联的版本及其流程数据
      const versionsToDelete = this.versions.filter(v => v.strategyId === strategyId)
      this.versions = this.versions.filter(v => v.strategyId !== strategyId)

      // 删除存储的流程图数据
      versionsToDelete.forEach(version => {
        localStorage.removeItem(`${STORAGE_KEYS.VERSION_FLOW_DATA}${version.id}`)
      })

      this.persistStrategies()
      this.persistVersions()
    },

    // 更新版本备注
    updateVersionRemark(versionId: string, newRemark: string) {
      const version = this.versions.find(v => v.id === versionId)
      if (version) {
        version.remark = newRemark
        this.persistVersions()
      }
    },

    // 删除单个版本
    removeVersion(versionId: string) {
      const version = this.versions.find(v => v.id === versionId)
      this.versions = this.versions.filter(v => v.id !== versionId)

      // 删除存储的流程图数据
      if (version) {
        localStorage.removeItem(`${STORAGE_KEYS.VERSION_FLOW_DATA}${versionId}`)
      }

      this.persistVersions()
    },

    // 新增策略
    addStrategy(name: string) {
      const now = new Date().toISOString()
      const id = `s${Date.now()}`
      const newStrategy: Strategy = {
        id,
        name,
        createdAt: now,
        updatedAt: now
      }
      this.strategies.push(newStrategy)
      this.persistStrategies()
      return id
    },

    // 新增版本（保存策略时调用）
    addVersion(
      strategyId: string,
      flowData?: { nodes: any[], edges: any[] },
      remark?: string
    ): string {
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

      // 如果有流程图数据，保存它
      if (flowData) {
        this.saveVersionFlowData(id, flowData)
      }

      this.persistVersions()
      return id
    },

    // 更新策略的 updatedAt
    touchStrategy(strategyId: string) {
      const strategy = this.strategies.find(s => s.id === strategyId)
      if (strategy) {
        strategy.updatedAt = new Date().toISOString()
        this.persistStrategies()
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
