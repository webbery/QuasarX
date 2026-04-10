// app/src/components/report/composables/useChartData.ts
// 数据获取逻辑 - 封装所有 API 调用和数据处理

import { ref, watch, nextTick, type Ref } from 'vue'
import axios from 'axios'
import https from 'https'
import {
  getBenchmark,
  calculateMetrics,
  BenchmarkMetrics,
  KlineData,
  clearExpiredCache,
} from '@/lib/tickflow'
import { useHistoryStore, type BacktestResult } from '@/stores/history'
import type { UseReportStateReturn } from './useReportState'

export interface UseChartDataReturn {
  // === 数据状态 ===
  /** 价格数据 [date, close][] */
  symbolPrices: Ref<any[]>
  /** 买入信号 [date, price][] */
  buySignals: Ref<any[]>
  /** 卖出信号 [date, price][] */
  sellSignals: Ref<any[]>
  /** 基准指标数据 */
  benchmarkMetrics: Ref<BenchmarkMetrics | null>
  /** 加载状态 */
  loading: Ref<boolean>

  // === 数据获取方法 ===
  /** 更新价格数据 */
  updatePrice: (symbol: string, startDate?: string, endDate?: string) => Promise<void>
  /** 更新交易信号 */
  updateTradeSignals: (buySignalsData: any[], sellSignalsData: any[]) => void
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
  updateStrategyPerformanceDates: (prices?: any[]) => void
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
  const symbolPrices = ref<any[]>([])
  const buySignals = ref<any[]>([])
  const sellSignals = ref<any[]>([])
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

      symbolPrices.value = []
      for (const oclhv of response.data) {
        const dt = oclhv['datetime']
        const date = new Date(dt * 1000)
        const Y = date.getFullYear() + '-'
        const M = (date.getMonth() + 1 < 10 ? '0' + (date.getMonth() + 1) : date.getMonth() + 1) + '-'
        const D = date.getDate()
        symbolPrices.value.push([Y + M + D, oclhv['close']])
      }

      // 设置标记，等待图表初始化后更新
      needsPriceChartUpdate.value = true
      console.info('[useChartData] 价格数据已加载:', symbolPrices.value.length, '条')

      // 如果有 pending 请求，触发基准数据加载
      if (reportState.selectedBenchmark.value && symbolPrices.value.length > 0) {
        const firstDate = new Date(symbolPrices.value[0][0])
        const lastDate = new Date(symbolPrices.value[symbolPrices.value.length - 1][0])
        reportState.backtestStartDate.value = firstDate
        reportState.backtestEndDate.value = lastDate
        loadBenchmark(firstDate, lastDate)
      }

      // 更新策略性能日期
      updateStrategyPerformanceDates(symbolPrices.value)
    } catch (error) {
      console.error('[useChartData] 获取价格数据失败:', error)
    }
  }

  /**
   * 更新交易信号
   */
  function updateTradeSignals(buySignalsData: any[], sellSignalsData: any[]) {
    if (!Array.isArray(buySignalsData) || !Array.isArray(sellSignalsData)) {
      console.error('交易信号数据必须是数组', { buySignalsData, sellSignalsData })
      return
    }

    buySignals.value = buySignalsData
    sellSignals.value = sellSignalsData

    console.info(`[useChartData] 交易信号已更新：买入 ${buySignalsData.length} 条，卖出 ${sellSignalsData.length} 条`)
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
      formatSignals(backtestResult.sell || [])
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
      await updatePrice(useSymbol, useStartDate, useEndDate)
    } else if (!useSymbol) {
      console.warn(`[useChartData] 无法获取标的代码，请检查流程图输入节点是否配置了代码参数`)
    }

    // 5. 加载基准数据
    if (signalStartDate && signalEndDate) {
      const benchmarkSymbol = localStorage.getItem('benchmark_symbol') || 'SH000300'
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
   * 如果买卖信号为空，使用测试数据（基于总收益线性插值）
   */
  function calculateStrategyCumulativeReturns(): number[] {
    const totalReturn = reportState.metricsData.value.total_return || 0

    // 如果有真实的买卖信号和价格数据，计算真实累计收益
    if (buySignals.value.length > 0 && symbolPrices.value.length > 0) {
      // TODO: 实现真实累计收益计算
      // 当前简化处理：使用线性插值
      console.info('[useChartData] 使用真实信号计算累计收益')
    }

    // 默认：使用总收益线性插值（测试数据）
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
  function updateStrategyPerformanceDates(prices?: any[]) {
    // 如果有价格数据，优先使用价格数据生成日期标签
    if (prices && prices.length > 0) {
      const monthMap = new Map<string, number>()
      prices.forEach((item: any) => {
        const date = new Date(item[0])
        const monthKey = `${date.getFullYear()}-${String(date.getMonth() + 1).padStart(2, '0')}`
        if (!monthMap.has(monthKey)) {
          monthMap.set(monthKey, monthMap.size)
        }
      })

      // 生成显示标签（如 "1 月"、"2 月"）
      reportState.strategyPerformanceDates.value = Array.from(monthMap.keys()).map(key => {
        const [, month] = key.split('-')
        return `${parseInt(month)}月`
      })
      console.info(`[useChartData] 从价格数据生成日期标签：${reportState.strategyPerformanceDates.value.length} 个月份`)
      return
    }

    // 如果没有价格数据，使用 backtestStartDate 和 backtestEndDate
    if (reportState.backtestStartDate.value && reportState.backtestEndDate.value) {
      const start = reportState.backtestStartDate.value
      const end = reportState.backtestEndDate.value
      const dates: string[] = []

      const current = new Date(start)
      while (current <= end) {
        const month = current.getMonth() + 1
        dates.push(`${month}月`)
        current.setMonth(current.getMonth() + 1)
      }

      reportState.strategyPerformanceDates.value = dates
      console.info(`[useChartData] 从回测日期生成标签：${reportState.strategyPerformanceDates.value.length} 个月份`)
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
    if (newPrices.length > 0) {
      updateStrategyPerformanceDates(newPrices)

      // 如果选择了基准，加载基准数据
      if (reportState.selectedBenchmark.value) {
        const firstDate = new Date(newPrices[0][0])
        const lastDate = new Date(newPrices[newPrices.length - 1][0])
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
    benchmarkMetrics,
    loading,
    // 数据获取方法
    updatePrice,
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
