import axios from 'axios'
import { keyMap } from '../../flow/nodeConfigs'
import { useHistoryStore } from '@/stores/history'
import { storeToRefs } from 'pinia'

/**
 * 回测执行与结果处理 composable
 * 处理回测执行、结果解析、图表更新等
 */
export function useBacktest(state, saveLoad, codeSync) {
  const {
    getNodes,
    getEdges,
    currentStrategyId,
    currentVersionId,
    hasUnsavedChanges,
    activeTab
  } = state

  const { validateFlow, ensureVersionId, strategies } = saveLoad
  const { syncCodeToDownstreamSignals } = codeSync

  /**
   * 执行回测
   */
  const runBacktest = async (reportViewRef, addInfoMessage, message, historyStore) => {
    // 1. 验证流程图
    if (!validateFlow()) {
      return
    }

    // 2. 确保有版本 ID 用于保存回测结果
    const versionId = await ensureVersionId(message)
    if (!versionId) {
      message.error('无法创建版本，回测中止')
      return
    }

    // 3. 同步 input 节点的 code 到所有可达的 signal 节点（确保回测前数据一致）
    const inputNodes = getNodes.value.filter(n => n.data.nodeType === 'input')
    for (const inputNode of inputNodes) {
      syncCodeToDownstreamSignals(inputNode, getEdges.value, getNodes.value, (nodeId, paramKey, value) => {
        // 这里需要回调到主组件的 updateNodeData
        // 但由于 operations 未传入，我们直接在节点上更新
        const nodeIndex = getNodes.value.findIndex(node => node.id === nodeId)
        if (nodeIndex !== -1) {
          getNodes.value[nodeIndex] = {
            ...getNodes.value[nodeIndex],
            data: {
              ...getNodes.value[nodeIndex].data,
              params: {
                ...getNodes.value[nodeIndex].data.params,
                [paramKey]: {
                  ...getNodes.value[nodeIndex].data.params[paramKey],
                  value
                }
              }
            }
          }
        }
      })
    }

    // 4. 获取当前图节点信息（使用动态生成的策略信息）
    const strategyName = currentStrategyId.value
      ? strategies.value.find(s => s.id === currentStrategyId.value)?.name || '未命名策略'
      : '临时策略'

    const curGraph = {
      id: currentStrategyId.value ? `strategy_${currentStrategyId.value}` : `temp_${Date.now()}`,
      name: strategyName,
      description: '用户自定义策略',
      nodes: getNodes.value,
      edges: getEdges.value
    }
    let graph = JSON.stringify(curGraph)
    // 替换中文键名
    for (const key in keyMap) {
      const src = keyMap[key]
      graph = graph.replaceAll(src, key)
    }

    try {
      const response = await axios.post('/v0/backtest', { script: graph })

      const result = response.data

      // 5. 传递指标数据到 ReportView
      updateMetricsInReportView(result, reportViewRef)

      // 6. 传递回测日期范围到 ReportView（用于获取基准数据）
      updateBenchmarkInReportView(result, reportViewRef)

      // 7. 解析回测结果中的交易历史数据
      updateTradeSignalsInReportView(result, reportViewRef)

      // 8. 保存回测结果到 historyStore
      saveBacktestResultToStore(versionId, result, graph, historyStore)

      // 9. 显示回测完成消息
      displayBacktestCompleteMessage(result, addInfoMessage)

      // 10. 获取信号节点的代码和日期范围，更新价格图表
      updatePriceChart(reportViewRef, addInfoMessage)

    } catch (error) {
      handleBacktestError(error, addInfoMessage, message)
    }
  }

  /**
   * 更新报表中的指标数据
   */
  const updateMetricsInReportView = (result, reportViewRef) => {
    try {
      if (reportViewRef?.value && reportViewRef.value.updateMetrics) {
        const features = result.features || {}
        const summary = result.summary || {}
        // summary 中的性能指标作为 features 的补充
        const summaryMetricKeys = ['sharp', 'annual_return', 'total_return', 'max_drawdown', 'win_rate', 'calmar_ratio']
        const mergedMetrics = { ...features }
        for (const key of summaryMetricKeys) {
          if (summary[key] !== undefined && (mergedMetrics[key] === undefined || mergedMetrics[key] === 0)) {
            mergedMetrics[key] = summary[key]
          }
        }
        reportViewRef.value.updateMetrics(mergedMetrics)
      } else {
        console.warn('ReportView 组件未找到 updateMetrics 方法')
      }
    } catch (metricsError) {
      console.error('传递指标数据时出错:', metricsError)
    }
  }

  /**
   * 更新报表中的基准数据
   */
  const updateBenchmarkInReportView = (result, reportViewRef) => {
    try {
      const buySignals = result.buy || []
      const sellSignals = result.sell || []
      const allSignals = [...buySignals, ...sellSignals]

      if (allSignals.length > 0) {
        const timestamps = allSignals.map(s => s[1])
        const minTime = Math.min(...timestamps)
        const maxTime = Math.max(...timestamps)
        const startDate = new Date(minTime * 1000)
        const endDate = new Date(maxTime * 1000)

        const benchmarkSymbol = localStorage.getItem('benchmark_symbol') || 'SH000300'

        if (reportViewRef?.value && reportViewRef.value.updateBenchmark) {
          reportViewRef.value.updateBenchmark({
            symbol: benchmarkSymbol,
            name: '',
            startDate,
            endDate
          })
        }
      }
    } catch (dateError) {
      console.warn('传递回测日期范围时出错:', dateError)
    }
  }

  /**
   * 更新报表中的交易信号
   */
  const updateTradeSignalsInReportView = (result, reportViewRef) => {
    try {
      const buySignals = result.buy || []
      const sellSignals = result.sell || []

      const formatSignals = (signals) => {
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

      if (reportViewRef?.value && reportViewRef.value.updateTradeSignals) {
        reportViewRef.value.updateTradeSignals(
          formatSignals(buySignals),
          formatSignals(sellSignals),
          buySignals,
          sellSignals
        )
      } else {
        console.warn('ReportView 组件未找到 updateTradeSignals 方法')
      }
    } catch (parseError) {
      console.error('解析回测交易历史数据时出错:', parseError)
    }
  }

  /**
   * 保存回测结果到 store
   */
  const saveBacktestResultToStore = (versionId, result, graph, historyStore) => {
    try {
      const backtestResult = {
        backtestTime: new Date().toISOString(),
        features: result.features || {},
        summary: result.summary || {},
        buy: result.buy || [],
        sell: result.sell || [],
        script: graph
      }
      historyStore.saveBacktestResult(versionId, backtestResult)
      console.info(`回测结果已保存到版本 ${versionId}`)
    } catch (saveError) {
      console.error('保存回测结果失败:', saveError)
    }
  }

  /**
   * 显示回测完成消息
   */
  const displayBacktestCompleteMessage = (result, addInfoMessage) => {
    const summary = result.summary || {}
    const buyCount = summary.buy_count || (result.buy?.length || 0)
    const sellCount = summary.sell_count || (result.sell?.length || 0)
    const sharp = result.features['sharp']

    addInfoMessage(`回测完成：${buyCount}笔买入，${sellCount}笔卖出，夏普率${sharp}`, 'success')
  }

  /**
   * 更新价格图表
   */
  const updatePriceChart = (reportViewRef, addInfoMessage) => {
    // 获取信号节点的代码和日期范围
    let signalNode = null
    for (const node of getNodes.value) {
      if (node.data.nodeType === 'signal') {
        signalNode = node
        break
      }
    }

    if (signalNode) {
      const codes = signalNode.data.params['代码']?.value
      const rangeDate = signalNode.data.params['回测周期']?.value

      if (codes && rangeDate && rangeDate.length === 2) {
        const symbols = Array.isArray(codes) ? codes : codes.split(',').map(s => s.trim()).filter(s => s.length > 0)
        if (symbols.length > 0 && reportViewRef?.value?.updatePrice) {
          // 设置所有标的到 select 选项
          if (reportViewRef.value.setSelectedSymbol) {
            reportViewRef.value.setSelectedSymbol(symbols)
          }
          // 只加载第一个标的的价格
          reportViewRef.value.updatePrice(symbols[0], rangeDate[0], rangeDate[1])
        }
      }
    } else {
      console.warn('未找到信号节点，无法更新价格图表')
      addInfoMessage('未找到信号节点，价格图表将不会更新', 'warning')
    }
  }

  /**
   * 处理回测错误
   */
  const handleBacktestError = (error, addInfoMessage, message) => {
    const exceptionWhat = error.response?.headers?.['exception_what'] ||
                           error.response?.headers?.['EXCEPTION_WHAT'] ||
                           error.response?.headers?.['Exception-What'] ||
                           error.message ||
                           '未知错误'

    const status = error.response?.status
    let userMessage = '回测失败'
    if (status === 400) {
      userMessage = '策略脚本格式错误，请检查节点配置'
    } else if (status === 404) {
      userMessage = '策略文件未找到'
    } else if (status === 500) {
      userMessage = `服务器错误：${exceptionWhat}`
    } else {
      userMessage = `错误：${exceptionWhat}`
    }

    message.error(userMessage)
    addInfoMessage(userMessage, 'error')
  }

  return {
    runBacktest
  }
}
