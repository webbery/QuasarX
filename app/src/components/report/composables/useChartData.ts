// app/src/components/report/composables/useChartData.ts
// 数据获取逻辑 - 封装所有 API 调用和数据处理

import { ref, watch, type Ref } from 'vue'
import axios from 'axios'
import https from 'https'
import {
  getBenchmark,
  BenchmarkMetrics,
  KlineData,
  clearExpiredCache,
} from '@/lib/tickflow'
import { useHistoryStore, type BacktestResult } from '@/stores/history'
import type { UseReportStateReturn } from './useReportState'

export interface UseChartDataReturn {
  // === 数据状态 ===
  /** 价格数据 Record<symbol, [date, close][]> */
  symbolPrices: Ref<Record<string, [string, number][]>>
  /** 买入信号 [date, price][] */
  buySignals: Ref<any[]>
  /** 卖出信号 [date, price][] */
  sellSignals: Ref<any[]>
  /** 原始买入信号 [symbol, timestamp, quantity, price][] */
  rawBuySignals: Ref<any[]>
  /** 原始卖出信号 [symbol, timestamp, quantity, price][] */
  rawSellSignals: Ref<any[]>
  /** 基准指标数据 */
  benchmarkMetrics: Ref<BenchmarkMetrics | null>
  /** 加载状态 */
  loading: Ref<boolean>

  // === 数据获取方法 ===
  /** 更新单个标的价格数据 */
  updatePrice: (symbol: string, startDate?: string, endDate?: string) => Promise<void>
  /** 设置所有标的（用于填充 select 选项） */
  setSelectedSymbol: (symbols: string[]) => void
  /** 更新多个标的的价格数据 */
  updatePriceForAll: (symbols: string[], startDate?: string, endDate?: string) => Promise<void>
  /** 获取单个标的的价格数据 */
  getPricesForSymbol: (symbol: string) => [string, number][]
  /** 更新交易信号 */
  updateTradeSignals: (buySignalsData: any[], sellSignalsData: any[], rawBuy?: any[], rawSell?: any[]) => void
  /** 更新策略指标 */
  updateMetrics: (features: Record<string, number>) => void
  /** 更新基准数据 */
  updateBenchmark: (data: { symbol: string; name: string; startDate: Date; endDate: Date }) => void
  /** 加载基准数据 */
  loadBenchmark: (start: Date, end: Date) => Promise<void>
  /** 刷新基准数据（清除缓存） */
  refreshBenchmark: () => void
  /** 从版本加载回测结果 */
  loadBacktestResultFromVersion: (versionId: string, startDate?: string, endDate?: string, symbol?: string) => Promise<void>

  // === 数据计算 ===
  /** 计算策略累计收益（从买卖信号 + 价格） */
  calculateStrategyCumulativeReturns: () => number[]
  /** 计算基准累计收益 */
  calculateBenchmarkCumulativeReturns: () => number[]
  /** 更新策略性能日期标签 */
  updateStrategyPerformanceDates: (prices?: [string, number][]) => void
  /** 更新策略性能数据 */
  updateStrategyPerformanceData: () => void
}

/**
 * 格式化日期为 YYYY-MM-DD 格式
 */
function formatDateTime(date: Date): string {
  const Y = date.getFullYear() + '-'
  const M = (date.getMonth() + 1 < 10 ? '0' + (date.getMonth() + 1) : date.getMonth() + 1) + '-'
  const D = (date.getDate() < 10 ? '0' + date.getDate() : date.getDate())
  return Y + M + D
}

/**
 * 数据获取 Hook
 * @param reportState 报告状态实例（共享状态）
 */
export function useChartData(
  reportState: UseReportStateReturn
): UseChartDataReturn {
  // === 数据状态 ===
  const symbolPrices = ref<Record<string, [string, number][]>>({})
  const buySignals = ref<any[]>([])
  const sellSignals = ref<any[]>([])
  const rawBuySignals = ref<any[]>([])
  const rawSellSignals = ref<any[]>([])
  const benchmarkMetrics = ref<BenchmarkMetrics | null>(null)
  const loading = ref(false)

  // === Pending 状态（用于图表未初始化时的数据缓存） ===
  const pendingPriceRequest = ref<{
    symbol: string
    startDate?: string
    endDate?: string
  } | null>(null)
  const needsPriceChartUpdate = ref(false)

  // === 数据获取方法 ===

  /**
   * 更新价格数据
   */
  async function updatePrice(symbol: string, startDate?: string, endDate?: string) {
    const server = localStorage.getItem('remote')
    const token = localStorage.getItem('token')
    const url = 'https://' + server + '/v0/stocks/history'
    const agent = new https.Agent({ rejectUnauthorized: false })

    let startTimestamp: number
    let endTimestamp: number

    if (startDate && endDate) {
      startTimestamp = Math.floor(new Date(startDate).getTime() / 1000)
      endTimestamp = Math.floor(new Date(endDate).getTime() / 1000)
    } else {
      startTimestamp = Date.parse('2010-01-01') / 1000
      endTimestamp = Date.parse('2025-01-01') / 1000
    }

    const params = {
      id: symbol,
      type: '1d',
      start: startTimestamp,
      end: endTimestamp,
      right: 1  // 后复权
    }

    try {
      const response = await axios.get(url, {
        params,
        httpsAgent: agent,
        headers: { 'Authorization': token }
      })

      if (response.status !== 200) return

      if (!Array.isArray(response.data) || response.data.length === 0) {
        console.warn('[useChartData] 价格数据为空或格式不正确')
        return
      }

      const prices: [string, number][] = []
      for (const oclhv of response.data) {
        const dt = oclhv['datetime']
        if (dt === undefined) continue
        const date = new Date(dt * 1000)
        const Y = date.getFullYear() + '-'
        const M = (date.getMonth() + 1 < 10 ? '0' + (date.getMonth() + 1) : date.getMonth() + 1) + '-'
        const D = date.getDate()
        prices.push([Y + M + D, oclhv['close']])
      }
      symbolPrices.value[symbol] = prices

      // 设置标记，等待图表初始化后更新
      needsPriceChartUpdate.value = true
      console.info('[useChartData] 价格数据已加载:', symbol, prices.length, '条')

      // 如果有 pending 请求，触发基准数据加载
      if (reportState.selectedBenchmark.value && prices.length > 0) {
        const firstDate = new Date(prices[0][0])
        const lastDate = new Date(prices[prices.length - 1][0])
        reportState.backtestStartDate.value = firstDate
        reportState.backtestEndDate.value = lastDate
        loadBenchmark(firstDate, lastDate)
      }

      // 更新策略性能日期
      updateStrategyPerformanceDates(prices)
    } catch (error) {
      console.error('[useChartData] 获取价格数据失败:', error)
    }
  }

  /**
   * 设置所有标的（用于填充 select 选项）
   */
  function setSelectedSymbol(symbols: string[]) {
    reportState.selectedSymbol.value = symbols
    console.info(`[useChartData] 设置标的列表：${symbols.join(', ')}`)
  }

  /**
   * 获取单个标的的价格数据
   */
  function getPricesForSymbol(symbol: string): [string, number][] {
    return symbolPrices.value[symbol] || []
  }

  /**
   * 更新多个标的的价格数据
   */
  async function updatePriceForAll(symbols: string[], startDate?: string, endDate?: string) {
    for (const symbol of symbols) {
      await updatePrice(symbol, startDate, endDate)
    }
  }

  /**
   * 更新交易信号
   */
  function updateTradeSignals(
    buySignalsData: any[],
    sellSignalsData: any[],
    rawBuy?: any[],
    rawSell?: any[]
  ) {
    rawBuySignals.value = rawBuy || []
    rawSellSignals.value = rawSell || []

    if (!Array.isArray(buySignalsData) || !Array.isArray(sellSignalsData)) {
      console.error('交易信号数据必须是数组', { buySignalsData, sellSignalsData })
      return
    }

    buySignals.value = buySignalsData
    sellSignals.value = sellSignalsData

    console.info(`[useChartData] 交易信号已更新：买入 ${buySignalsData.length} 条，卖出 ${sellSignalsData.length} 条`)

    // 触发收益重算
    updateStrategyPerformanceData()
  }

  /**
   * 更新策略指标
   */
  function updateMetrics(features: Record<string, number>) {
    if (!features || typeof features !== 'object') {
      console.error('指标数据必须是对象', features)
      return
    }

    reportState.metricsData.value = features
    console.info('[useChartData] 策略指标已更新:', features)

    // 更新策略性能数据
    updateStrategyPerformanceData()
  }

  /**
   * 更新基准数据（外部调用）
   */
  function updateBenchmark(data: { symbol: string; name: string; startDate: Date; endDate: Date }) {
    reportState.backtestStartDate.value = data.startDate
    reportState.backtestEndDate.value = data.endDate
    if (data.symbol) {
      reportState.selectedBenchmark.value = data.symbol
      loadBenchmark(data.startDate, data.endDate)
    }
  }

  /**
   * 加载基准数据
   */
  async function loadBenchmark(start: Date, end: Date) {
    if (!reportState.selectedBenchmark.value) {
      benchmarkMetrics.value = null
      reportState.benchmarkData.value = []
      reportState.benchmarkName.value = ''
      return
    }

    loading.value = true
    try {
      const result = await getBenchmark(reportState.selectedBenchmark.value, start, end)
      benchmarkMetrics.value = result.metrics
      reportState.benchmarkData.value = result.data
      reportState.benchmarkName.value = result.name
      console.info(`[useChartData] 基准数据已加载：${result.name}`)
    } catch (e) {
      console.error('[useChartData] 获取基准数据失败:', e)
    } finally {
      loading.value = false
    }
  }

  /**
   * 刷新基准数据（清除缓存后重新加载）
   */
  function refreshBenchmark() {
    clearExpiredCache().then(() => {
      if (reportState.backtestStartDate.value && reportState.backtestEndDate.value) {
        loadBenchmark(reportState.backtestStartDate.value, reportState.backtestEndDate.value)
      }
    })
  }

  /**
   * 从版本加载回测结果
   */
  async function loadBacktestResultFromVersion(
    versionId: string,
    startDate?: string,
    endDate?: string,
    symbol?: string
  ) {
    const historyStore = useHistoryStore()
    const backtestResult = await historyStore.loadBacktestResult(versionId)

    if (!backtestResult) {
      console.info(`[useChartData] 版本 ${versionId} 没有回测结果`)
      return
    }

    console.info(`[useChartData] 加载版本 ${versionId} 的回测结果`)

    // 1. 恢复策略指标
    updateMetrics(backtestResult.features || {})

    // 2. 恢复交易信号
    const formatSignals = (signals: any[]) => {
      return signals.map(signal => {
        const timestamp = signal[1]
        const price = signal[3]
        const date = new Date(timestamp * 1000)
        const Y = date.getFullYear() + '-'
        const M = (date.getMonth() + 1 < 10 ? '0' + (date.getMonth() + 1) : date.getMonth() + 1) + '-'
        const D = date.getDate()
        return [Y + M + D, price]
      })
    }

    updateTradeSignals(
      formatSignals(backtestResult.buy || []),
      formatSignals(backtestResult.sell || []),
      backtestResult.buy || [],   // 原始数据
      backtestResult.sell || []   // 原始数据
    )

    // 3. 提取标的代码和日期范围
    const allSignals = [...(backtestResult.buy || []), ...(backtestResult.sell || [])]
    let signalStartDate: Date | null = null
    let signalEndDate: Date | null = null
    let signalSymbol = ''

    if (allSignals.length > 0) {
      const timestamps = allSignals.map(s => s[1])
      const minTime = Math.min(...timestamps)
      const maxTime = Math.max(...timestamps)
      signalStartDate = new Date(minTime * 1000)
      signalEndDate = new Date(maxTime * 1000)
      signalSymbol = backtestResult.buy?.[0]?.[0] || backtestResult.sell?.[0]?.[0] || ''
    }

    const useStartDate = startDate || (signalStartDate ? formatDateTime(signalStartDate) : null)
    const useEndDate = endDate || (signalEndDate ? formatDateTime(signalEndDate) : null)
    const useSymbol = symbol || signalSymbol

    // 设置 selectedSymbol 数组
    if (useSymbol) {
      const symbols = useSymbol.split(',').map(s => s.trim()).filter(s => s.length > 0)
      if (symbols.length > 0) {
        reportState.selectedSymbol.value = symbols
        console.info(`[useChartData] 设置标的代码列表：${reportState.selectedSymbol.value}`)
      }
    }

    // 4. 获取历史价格数据
    if (useSymbol && useStartDate && useEndDate) {
      console.info(`[useChartData] 获取历史价格：${useSymbol}, ${useStartDate} - ${useEndDate}`)
      const symbols = useSymbol.split(',').map(s => s.trim()).filter(s => s.length > 0)
      await updatePriceForAll(symbols, useStartDate, useEndDate)
    } else if (!useSymbol) {
      console.warn(`[useChartData] 无法获取标的代码，请检查流程图输入节点是否配置了代码参数`)
    }

    // 5. 加载基准数据
    if (signalStartDate && signalEndDate) {
      const benchmarkSymbol = localStorage.getItem('benchmark_symbol') || 'SH000001'
      updateBenchmark({
        symbol: benchmarkSymbol,
        name: '',
        startDate: signalStartDate,
        endDate: signalEndDate
      })
    }

    console.info(`[useChartData] 回测结果已恢复：${backtestResult.buy?.length || 0} 笔买入，${backtestResult.sell?.length || 0} 笔卖出`)
  }

  // === 数据计算方法 ===

  /**
   * 计算策略累计收益（从买卖信号 + 价格）
   *
   * 算法：
   * 1. 合并买卖信号为 Trade 数组，按 timestamp 排序
   * 2. 初始资金 = 首日所有买入的总成本（quantity × price）
   * 3. 遍历价格日期，对每个日期：
   *    - 处理该日期及之前的所有交易
   *    - 组合价值 = 现金 + Σ(持仓qty × 当日收盘价)
   *    - 收益率 = (组合价值 - 初始资金) / 初始资金 × 100
   * 4. 降级处理：无信号时仍用线性插值
   */
  function calculateStrategyCumulativeReturns(): number[] {
    const totalReturn = reportState.metricsData.value.total_return || 0

    // 如果有真实的买卖信号和价格数据，计算真实累计收益
    if (rawBuySignals.value.length > 0 && Object.keys(symbolPrices.value).length > 0) {
      console.info('[useChartData] 使用真实信号计算累计收益')

      // 构建价格映射：date_string -> close (遍历所有标的)
      const priceMap = new Map<string, number>()
      for (const symbol of Object.keys(symbolPrices.value)) {
        for (const [date, close] of symbolPrices.value[symbol]) {
          priceMap.set(date, close)
        }
      }

      // 将原始信号转为 Trade 对象并按 timestamp 排序
      interface Trade {
        symbol: string
        timestamp: number
        quantity: number
        price: number
        dateStr: string
        type: 'buy' | 'sell'
      }

      const toTrade = (signal: any, type: 'buy' | 'sell'): Trade => {
        const [symbol, timestamp, quantity, price] = signal
        const d = new Date(timestamp * 1000)
        const dateStr = `${d.getFullYear()}-${String(d.getMonth() + 1).padStart(2, '0')}-${String(d.getDate()).padStart(2, '0')}`
        return { symbol, timestamp, quantity, price, dateStr, type }
      }

      const allTrades: Trade[] = [
        ...rawBuySignals.value.map(s => toTrade(s, 'buy')),
        ...rawSellSignals.value.map(s => toTrade(s, 'sell'))
      ].sort((a, b) => a.timestamp - b.timestamp)

      // 获取所有交易日期（去重，排序）
      const tradeDates = [...new Set(allTrades.map(t => t.dateStr))].sort()

      // 计算初始资金 = 第一笔交易日所有买入的总成本
      const firstTradeDate = tradeDates[0]
      const initialCapital = allTrades
        .filter(t => t.type === 'buy' && t.dateStr === firstTradeDate)
        .reduce((sum, t) => sum + t.quantity * t.price, 0)

      if (initialCapital <= 0) {
        console.warn('[useChartData] 初始资金计算异常，降级为线性插值')
      } else {
        // 只保留交易期内的价格日期
        const minTradeDate = allTrades[0].dateStr
        const maxTradeDate = allTrades[allTrades.length - 1].dateStr
        // 收集所有标的的价格日期
        const allPriceDates = new Set<string>()
        for (const symbol of Object.keys(symbolPrices.value)) {
          for (const [date] of symbolPrices.value[symbol]) {
            allPriceDates.add(date)
          }
        }
        const sortedPriceDates = [...allPriceDates]
          .filter(d => d >= minTradeDate && d <= maxTradeDate)
          .sort()

        const returns: number[] = []
        const dates: string[] = []

        let cash = initialCapital // 初始资金池
        const positions = new Map<string, number>() // symbol -> quantity

        let tradeIndex = 0
        let prevPortfolioValue = initialCapital // 前一日组合价值（用于时间加权收益）

        for (const dateStr of sortedPriceDates) {
          // 计算交易前的组合价值（反映真实价格变动）
          let portfolioValueBeforeTrades = cash
          for (const [symbol, qty] of positions) {
            const closePrice = priceMap.get(dateStr)
            if (closePrice !== undefined) {
              portfolioValueBeforeTrades += qty * closePrice
            }
          }

          // 时间加权收益率：与前一日的组合价值比较
          if (dateStr === minTradeDate) {
            returns.push(0) // 首日归零
          } else {
            const dailyReturn = ((portfolioValueBeforeTrades - prevPortfolioValue) / prevPortfolioValue) * 100
            returns.push(Number(dailyReturn.toFixed(2)))
          }

          // 处理该日期的所有交易
          while (tradeIndex < allTrades.length && allTrades[tradeIndex].dateStr <= dateStr) {
            const trade = allTrades[tradeIndex]
            const currentQty = positions.get(trade.symbol) || 0

            if (trade.type === 'buy') {
              positions.set(trade.symbol, currentQty + trade.quantity)
              cash -= trade.quantity * trade.price
            } else {
              const newQty = Math.max(currentQty - trade.quantity, 0)
              if (newQty === 0) positions.delete(trade.symbol)
              else positions.set(trade.symbol, newQty)
              cash += trade.quantity * trade.price
            }

            tradeIndex++
          }

          // 更新前一日组合价值（交易后的价值）
          prevPortfolioValue = cash
          for (const [symbol, qty] of positions) {
            const closePrice = priceMap.get(dateStr)
            if (closePrice !== undefined) {
              prevPortfolioValue += qty * closePrice
            }
          }

          dates.push(dateStr)
        }

        // 更新策略性能日期为实际日期字符串
        reportState.strategyPerformanceDates.value = dates

        return returns
      }
    }

    // 降级：使用总收益线性插值（无信号时的默认行为）
    const numDates = reportState.strategyPerformanceDates.value.length || 12
    const returns: number[] = []

    for (let i = 0; i < numDates; i++) {
      const progress = numDates > 1 ? i / (numDates - 1) : 0
      returns.push(Number((totalReturn * progress * 100).toFixed(2)))
    }

    return returns
  }

  /**
   * 计算基准累计收益
   */
  function calculateBenchmarkCumulativeReturns(): number[] {
    if (reportState.benchmarkData.value.length === 0) return []

    const benchmarkData = reportState.benchmarkData.value as KlineData[]
    const firstClose = benchmarkData[0].close

    return benchmarkData.map(d =>
      Number((((d.close - firstClose) / firstClose) * 100).toFixed(2))
    )
  }

  /**
   * 更新策略性能日期标签（从价格数据或回测日期范围生成）
   */
  function updateStrategyPerformanceDates(prices?: [string, number][]) {
    // 如果有价格数据，使用日级日期字符串
    if (prices && prices.length > 0) {
      reportState.strategyPerformanceDates.value = prices.map((item: any) => item[0])
      console.info(`[useChartData] 从价格数据生成日期标签：${reportState.strategyPerformanceDates.value.length} 天`)
      return
    }

    // 如果没有价格数据，使用 backtestStartDate 和 backtestEndDate 生成日级标签
    if (reportState.backtestStartDate.value && reportState.backtestEndDate.value) {
      const start = reportState.backtestStartDate.value
      const end = reportState.backtestEndDate.value
      const dates: string[] = []

      const current = new Date(start)
      while (current <= end) {
        const Y = current.getFullYear()
        const M = String(current.getMonth() + 1).padStart(2, '0')
        const D = String(current.getDate()).padStart(2, '0')
        dates.push(`${Y}-${M}-${D}`)
        current.setDate(current.getDate() + 1)
      }

      reportState.strategyPerformanceDates.value = dates
      console.info(`[useChartData] 从回测日期生成日级标签：${reportState.strategyPerformanceDates.value.length} 天`)
    }
  }

  /**
   * 更新策略性能数据
   */
  function updateStrategyPerformanceData() {
    reportState.strategyPerformanceData.value = calculateStrategyCumulativeReturns()
    console.info(`[useChartData] 策略性能数据已更新：${reportState.strategyPerformanceData.value.length} 个点`)
  }

  // === Watchers：监听状态变化自动处理 ===

  // 监听 symbolPrices 变化，更新策略性能日期和基准数据
  watch(symbolPrices, (newPrices) => {
    const keys = Object.keys(newPrices)
    if (keys.length > 0 && newPrices[keys[0]].length > 0) {
      updateStrategyPerformanceDates(newPrices[keys[0]])

      // 如果选择了基准，加载基准数据
      if (reportState.selectedBenchmark.value) {
        const firstDate = new Date(newPrices[keys[0]][0][0])
        const lastDate = new Date(newPrices[keys[0]][newPrices[keys[0]].length - 1][0])
        reportState.backtestStartDate.value = firstDate
        reportState.backtestEndDate.value = lastDate
        loadBenchmark(firstDate, lastDate)
      }
    }
  }, { deep: true })

  // 监听回测日期范围变化，更新策略性能数据
  watch(
    [reportState.backtestStartDate, reportState.backtestEndDate],
    ([start, end]) => {
      if (start && end) {
        console.info('[useChartData] 回测日期范围已更新:', formatDateTime(start), 'to', formatDateTime(end))
        updateStrategyPerformanceDates()
        updateStrategyPerformanceData()
      }
    },
    { immediate: false }
  )

  return {
    // 数据状态
    symbolPrices,
    buySignals,
    sellSignals,
    rawBuySignals,
    rawSellSignals,
    benchmarkMetrics,
    loading,
    // 数据获取方法
    updatePrice,
    setSelectedSymbol,
    updatePriceForAll,
    getPricesForSymbol,
    updateTradeSignals,
    updateMetrics,
    updateBenchmark,
    loadBenchmark,
    refreshBenchmark,
    loadBacktestResultFromVersion,
    // 数据计算
    calculateStrategyCumulativeReturns,
    calculateBenchmarkCumulativeReturns,
    updateStrategyPerformanceDates,
    updateStrategyPerformanceData
  }
}
