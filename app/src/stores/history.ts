import { defineStore } from 'pinia'

export interface Strategy {
  id: string
  name: string
}

export interface Version {
  id: string
  strategyId: string
  saveTime: string   // ISO 字符串
  remark: string
}

// 辅助函数：生成默认备注（格式化时间）
function formatDate(isoString: string): string {
  const date = new Date(isoString)
  return `${date.getFullYear()}-${String(date.getMonth() + 1).padStart(2, '0')}-${String(date.getDate()).padStart(2, '0')} ${String(date.getHours()).padStart(2, '0')}:${String(date.getMinutes()).padStart(2, '0')}`
}

export const useHistoryStore = defineStore('history', {
  state: () => ({
    strategies: [
      { id: 's1', name: '策略A' },
      { id: 's2', name: '策略B' },
      { id: 's3', name: '策略C' }
    ] as Strategy[],
    versions: [
      { id: 'v1', strategyId: 's1', saveTime: '2023-01-01T12:00:00', remark: formatDate('2023-01-01T12:00:00') },
      { id: 'v2', strategyId: 's1', saveTime: '2023-02-01T15:30:00', remark: '重要版本' },
      { id: 'v3', strategyId: 's2', saveTime: '2023-03-01T09:20:00', remark: formatDate('2023-03-01T09:20:00') },
      { id: 'v4', strategyId: 's3', saveTime: '2023-04-01T18:45:00', remark: formatDate('2023-04-01T18:45:00') },
      { id: 'v5', strategyId: 's3', saveTime: '2023-05-01T08:10:00', remark: '测试版本' },
      { id: 'v6', strategyId: 's3', saveTime: '2023-06-01T22:30:00', remark: formatDate('2023-06-01T22:30:00') }
    ] as Version[]
  }),

  actions: {
    // 更新策略名称
    updateStrategyName(strategyId: string, newName: string) {
      const strategy = this.strategies.find(s => s.id === strategyId)
      if (strategy) strategy.name = newName
    },

    // 删除策略及其所有版本
    removeStrategy(strategyId: string) {
      this.strategies = this.strategies.filter(s => s.id !== strategyId)
      this.versions = this.versions.filter(v => v.strategyId !== strategyId)
    },

    // 更新版本备注
    updateVersionRemark(versionId: string, newRemark: string) {
      const version = this.versions.find(v => v.id === versionId)
      if (version) version.remark = newRemark
    },

    // 删除单个版本
    removeVersion(versionId: string) {
      this.versions = this.versions.filter(v => v.id !== versionId)
    },

    // 新增策略（可选扩展）
    addStrategy(name: string) {
      const id = `s${Date.now()}`
      this.strategies.push({ id, name })
    },

    // 新增版本（可选扩展）
    addVersion(strategyId: string, saveTime: string, remark?: string) {
      const id = `v${Date.now()}`
      const finalRemark = remark ?? formatDate(saveTime)
      this.versions.push({ id, strategyId, saveTime, remark: finalRemark })
    }
  }
})