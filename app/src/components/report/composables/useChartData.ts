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
  /** 策略收益曲线是否为估算（无真实信号时的线性插值） */
  strategyReturnsEstimated: Ref<boolean>

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
  updateTradeSignals: (buySignalsData: any[], sellSignalsData: any[], rawBuy?: any[], rawSell?: any[], dailyReturnsData?: { returns: number[]; dates: number[] }) => void
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
  /** 计算策略日收益率（从买卖信号 + 价格） */
  calculateStrategyDailyReturns: () => number[]
  /** 计算基准累计收益 */
  calculateBenchmarkCumulativeReturns: () => number[]
  /** 更新策略性能日期标签 */
  updateStrategyPerformanceDates: (prices?: [string, number][]) => void
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
  const strategyReturnsEstimated = ref(false)

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
   * @param dailyReturnsData 可选，后端返回的每日收益率 { returns: number[] (小数), dates: number[] (Unix秒) }
   */
  function updateTradeSignals(
    buySignalsData: any[],
    sellSignalsData: any[],
    rawBuy?: any[],
    rawSell?: any[],
    dailyReturnsData?: { returns: number[]; dates: number[] }
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

    // 如果后端提供了收益率数据，直接使用（覆盖前端计算）
    if (dailyReturnsData && dailyReturnsData.returns && dailyReturnsData.returns.length > 0) {
      const { returns, dates } = dailyReturnsData
      // 后端小数 → 前端百分比
      reportState.strategyDailyReturns.value = returns.map(r => Number((r * 100).toFixed(4)))
      // 时间戳 → YYYY-MM-DD
      reportState.strategyPerformanceDates.value = dates.map(ts => {
        const d = new Date(ts * 1000)
        return `${d.getFullYear()}-${String(d.getMonth() + 1).padStart(2, '0')}-${String(d.getDate()).padStart(2, '0')}`
      })
      strategyReturnsEstimated.value = false
      console.info(`[useChartData] 后端收益率已注入：${returns.length} 个点`)
    } else {
      // 无后端数据，清空（不再前端计算）
      reportState.strategyDailyReturns.value = []
      reportState.strategyPerformanceDates.value = []
      console.warn('[useChartData] 后端未提供收益率数据，Strategy Performance 将为空')
    }
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
   * 计算策略日收益率
   *
   * 后端已通过 daily_returns / daily_dates 提供，前端不再自行计算。
   * 此函数仅保留作为占位，防止外部调用方崩溃。
   */
  function calculateStrategyDailyReturns(): number[] {
    return reportState.strategyDailyReturns.value.length > 0
      ? [...reportState.strategyDailyReturns.value]
      : []
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

  // 监听回测日期范围变化，更新日期标签
  watch(
    [reportState.backtestStartDate, reportState.backtestEndDate],
    ([start, end]) => {
      if (start && end) {
        console.info('[useChartData] 回测日期范围已更新:', formatDateTime(start), 'to', formatDateTime(end))
        updateStrategyPerformanceDates()
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
    strategyReturnsEstimated,
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
    calculateStrategyDailyReturns,
    calculateBenchmarkCumulativeReturns,
    updateStrategyPerformanceDates
  }
}
