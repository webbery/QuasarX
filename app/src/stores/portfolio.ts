import { defineStore } from 'pinia'
import type { FlowData } from '../lib/strategyPool'
import { extractSecuritiesFromFlowData, isAllStocks } from '../lib/strategyPool'

export interface ViewConfig {
  id: string
  type: 'absolute' | 'relative'
  security?: string
  expectedReturn?: number
  longSecurity?: string
  shortSecurity?: string
  expectedExcessReturn?: number
  confidence: number
}

export interface PortfolioConfig {
  id: string
  name: string
  strategyId: string
  createdAt: number
  updatedAt: number
  modelType: 'black_litterman' | 'mean_variance' | 'risk_parity'
  params: {
    benchmark: string
    riskFreeRate: number
    riskAversion: number
    marketVolatility: number
  }
  views: ViewConfig[]
  securities: Array<{ code: string; name: string }>
  constraints: {
    maxWeight: number
    minWeight: number
    longOnly: boolean
    fullyInvested: boolean
  }
  optimizationResult?: {
    expectedReturn: number
    volatility: number
    sharpeRatio: number
    maxDrawdown: number
    weights: Array<{
      code: string
      name: string
      weight: number
    }>
  }
}

export const usePortfolioStore = defineStore('portfolio', {
  state: () => ({
    // 当前策略图 ID
    currentStrategyId: '',
    // 组合配置列表
    portfolioConfigs: [] as PortfolioConfig[],
    // 当前选中的配置
    currentPortfolioConfig: null as PortfolioConfig | null,
    // 可用证券池（已废弃，改为按策略获取）
    availableSecurities: [] as Array<{ code: string; name: string }>,
    // 策略标的池缓存
    strategyPools: {} as Record<string, Array<{ code: string; name: string }>>
  }),

  getters: {
    // 获取指定策略图的所有配置
    getConfigsByStrategy: (state) => (strategyId: string) => {
      return state.portfolioConfigs.filter(c => c.strategyId === strategyId)
    },

    // 获取当前配置的名称
    currentConfigName: (state) => {
      return state.currentPortfolioConfig?.name || '未选择'
    },

    // 获取当前策略的标的池
    getCurrentStrategyPool: (state) => {
      return state.strategyPools[state.currentStrategyId] || []
    }
  },

  actions: {
    // 设置当前策略图 ID
    setCurrentStrategyId(strategyId: string) {
      this.currentStrategyId = strategyId
      // 清空当前选中的配置（因为策略图变了）
      this.currentPortfolioConfig = null
    },

    // 加载组合配置列表
    async loadPortfolioConfigs(strategyId: string) {
      try {
        // 先从本地存储加载
        const stored = localStorage.getItem(`portfolio_configs_${strategyId}`)
        if (stored) {
          this.portfolioConfigs = JSON.parse(stored)
        } else {
          // TODO: 从 API 加载
          // const response = await axios.get(`/v0/portfolio?strategyId=${strategyId}`)
          // this.portfolioConfigs = response.data
        }
      } catch (error) {
        console.error('加载组合配置失败:', error)
      }
    },

    // 加载选中的配置
    loadConfig(configId: string) {
      const config = this.portfolioConfigs.find(c => c.id === configId)
      if (config) {
        this.currentPortfolioConfig = JSON.parse(JSON.stringify(config))
      } else {
        this.currentPortfolioConfig = null
      }
    },

    // 创建新配置
    async createConfig(config: Partial<PortfolioConfig>) {
      const newConfig: PortfolioConfig = {
        id: 'port_' + Date.now(),
        name: config.name || '新组合',
        strategyId: this.currentStrategyId,
        createdAt: Date.now(),
        updatedAt: Date.now(),
        modelType: config.modelType || 'black_litterman',
        params: config.params || {
          benchmark: '000300.SH',
          riskFreeRate: 2.5,
          riskAversion: 2.5,
          marketVolatility: 15.0
        },
        views: config.views || [],
        securities: config.securities || [],
        constraints: config.constraints || {
          maxWeight: 20,
          minWeight: 0,
          longOnly: true,
          fullyInvested: true
        }
      }

      this.portfolioConfigs.push(newConfig)
      this.currentPortfolioConfig = newConfig
      this.currentPortfolioConfig = JSON.parse(JSON.stringify(newConfig))

      // 保存到本地存储
      await this.saveToStorage()

      return newConfig
    },

    // 更新配置
    async updateConfig(config: Partial<PortfolioConfig>) {
      const index = this.portfolioConfigs.findIndex(c => c.id === config.id)
      if (index !== -1) {
        this.portfolioConfigs[index] = {
          ...this.portfolioConfigs[index],
          ...config,
          updatedAt: Date.now()
        }
        this.currentPortfolioConfig = JSON.parse(JSON.stringify(this.portfolioConfigs[index]))
        await this.saveToStorage()
      }
    },

    // 删除配置
    async deleteConfig(configId: string) {
      const index = this.portfolioConfigs.findIndex(c => c.id === configId)
      if (index !== -1) {
        this.portfolioConfigs.splice(index, 1)
        if (this.currentPortfolioConfig?.id === configId) {
          this.currentPortfolioConfig = null
        }
        await this.saveToStorage()
      }
    },

    // 保存优化结果
    async saveOptimizationResult(result: PortfolioConfig['optimizationResult']) {
      if (this.currentPortfolioConfig) {
        this.currentPortfolioConfig.optimizationResult = result
        const config = this.portfolioConfigs.find(c => c.id === this.currentPortfolioConfig?.id)
        if (config) {
          config.optimizationResult = result
          config.updatedAt = Date.now()
        }
        await this.saveToStorage()
      }
    },

    // 保存到本地存储
    async saveToStorage() {
      if (this.currentStrategyId) {
        localStorage.setItem(
          `portfolio_configs_${this.currentStrategyId}`,
          JSON.stringify(this.portfolioConfigs)
        )
      }
    },

    // 设置可用证券池（已废弃，保留兼容）
    setAvailableSecurities(securities: Array<{ code: string; name: string }>) {
      this.availableSecurities = securities
    },

    // 从流程图数据加载策略标的池
    async loadStrategyPool(strategyId: string, flowData: FlowData) {
      try {
        const securities = extractSecuritiesFromFlowData(flowData)
        this.strategyPools[strategyId] = securities
        return securities
      } catch (error) {
        console.error('加载策略标的池失败:', error)
        this.strategyPools[strategyId] = []
        return []
      }
    },

    // 获取策略标的池（如果已缓存则直接返回）
    getStrategyPool(strategyId: string): Array<{ code: string; name: string }> {
      return this.strategyPools[strategyId] || []
    },

    // 检查是否是全市场股票
    isAllStocks(securities?: Array<{ code: string; name: string }>): boolean {
      const pool = securities || this.getCurrentStrategyPool
      return isAllStocks(pool)
    }
  }
})
