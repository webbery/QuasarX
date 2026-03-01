import { defineStore } from 'pinia'
import axios from 'axios'

export const useAccountStore = defineStore('account', {
  state: () => ({
    availableFunds: 0,
    frozenFunds: 0,
    totalValue: 0,
    increasePercent: 0,
    todayPL: 0,
    sharpRatio: 0,
    kamaRatio: 0,
    maxDrawDown: 0
  }),
  actions: {
    // 从 API 获取最新账户数据
    async fetchAccountData() {
      try {
        console.info('before todayPL:', this.todayPL)
        const response = await axios.get('/v0/user/funds')
        const data = response.data
        this.availableFunds = data.funds || 0
        this.frozenFunds = data.frozenFunds || 0
        this.totalValue = data.totalValue || 0
        this.increasePercent = data.increasePercent || 0
        // this.todayPL = data.todayProfitLoss || 0
        // this.sharpRatio = data.sharpRatio || 0
        // this.kamaRatio = data.kamaRatio || 0
        // this.maxDrawDown = data.maxDrawDown || 0
        console.info(' after todayPL:', this.todayPL)
    } catch (error) {
        console.error('获取账户数据失败:', error)
      }
    },
    // 手动更新可用资金（例如交易后直接更新）
    setAvailableFunds(value: number) {
      this.availableFunds = value
    },
    // 也可以直接调用 fetchAccountData 全量更新
  }
})