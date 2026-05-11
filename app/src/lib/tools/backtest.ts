/**
 * 策略回测 Tool
 *
 * Agent 可通过此工具：
 * - 执行回测并获取完整结果
 * - 查询回测指标（夏普、回撤、胜率等）
 * - 获取回测交易信号明细
 * - 获取回测摘要（易读文本格式）
 */

import { tool } from "@langchain/core/tools"
import { z } from "zod"
import { useHistoryStore } from "@/stores/history"
import type { BacktestResult } from "@/stores/history"
import { computeStatistics, calculateReturns } from "@/lib/statistics"
import { buildChartData } from "@/lib/chartData"

/**
 * 格式化指标为易读文本
 */
function formatMetrics(features: Record<string, number>): string {
  const fmt = (key: string, pct = false) => {
    const v = features[key]
    if (v === undefined || v === null) return "N/A"
    if (pct) return `${(v * 100).toFixed(2)}%`
    return typeof v === 'number' && Math.abs(v) < 100 ? v.toFixed(4) : String(v)
  }

  // 偏度/峰度解读
  const skew = features.skewness
  const kurt = features.kurtosis
  let distSection = ""
  if (skew !== undefined || kurt !== undefined) {
    const skewTag = skew !== undefined ?
      (skew > 0.5 ? "正偏（右偏，极端收益较多）" :
       skew < -0.5 ? "负偏（左偏，极端亏损较多）" :
       "近似对称") : "N/A"
    const kurtTag = kurt !== undefined ?
      (kurt > 3.5 ? "尖峰厚尾（极端事件风险高）" :
       kurt < 2.5 ? "低峰薄尾（分布较平坦）" :
       "接近正态分布") : "N/A"
    distSection = `\n\n**分布特征**\n  偏度: ${fmt('skewness')} (${skewTag})\n  峰度: ${fmt('kurtosis')} (${kurtTag})`
  }

  return [
    `**收益指标**`,
    `  总收益率: ${fmt('total_return', true)}`,
    `  年化收益: ${fmt('annual_return', true)}`,
    ``,
    `**风险指标**`,
    `  最大回撤: ${fmt('max_drawdown', true)}`,
    `  波动率: ${fmt('volatility', true)}`,
    `  在险价值(VAR): ${fmt('VAR', true)}`,
    `  预期损失(ES): ${fmt('ES', true)}`,
    ``,
    `**风险调整后收益**`,
    `  夏普比率: ${fmt('sharp')}`,
    `  卡玛比率: ${fmt('calmar_ratio')}`,
    `  信息比率: ${fmt('information_ratio')}`,
    ``,
    `**交易统计**`,
    `  胜率: ${fmt('win_rate', true)}`,
    `  总交易次数: ${features.num_trades ?? 'N/A'}`,
    distSection,
  ].filter(Boolean).join('\n')
}

/**
 * 格式化交易信号为易读文本
 */
function formatSignals(buy: any[], sell: any[]): string {
  if (!buy.length && !sell.length) return "无交易信号"

  const lines: string[] = []

  if (buy.length > 0) {
    lines.push(`**买入信号**（共 ${buy.length} 笔）：`)
    const recent = buy.slice(-5)
    for (const [symbol, ts, qty, price] of recent) {
      const date = new Date(ts * 1000).toLocaleDateString('zh-CN')
      lines.push(`  - ${date} 买入 ${symbol} ${qty}股 @ ${price}`)
    }
    if (buy.length > 5) lines.push(`  ...（仅显示最近 5 笔，共 ${buy.length} 笔）`)
  }

  if (sell.length > 0) {
    lines.push(``)
    lines.push(`**卖出信号**（共 ${sell.length} 笔）：`)
    const recent = sell.slice(-5)
    for (const [symbol, ts, qty, price] of recent) {
      const date = new Date(ts * 1000).toLocaleDateString('zh-CN')
      lines.push(`  - ${date} 卖出 ${symbol} ${qty}股 @ ${price}`)
    }
    if (sell.length > 5) lines.push(`  ...（仅显示最近 5 笔，共 ${sell.length} 笔）`)
  }

  return lines.join('\n')
}

/**
 * 从交易信号中近似计算偏度/峰度
 * 使用买卖价格序列计算收益率序列，再求统计量
 */
function computeDistributionFeatures(buy: any[], sell: any[]): { skewness: number; kurtosis: number } {
  const allPrices: number[] = []
  for (const [, , , price] of buy) {
    allPrices.push(price as number)
  }
  for (const [, , , price] of sell) {
    allPrices.push(price as number)
  }

  if (allPrices.length < 3) {
    return { skewness: 0, kurtosis: 0 }
  }

  const returns = calculateReturns(allPrices)
  if (returns.length < 2) {
    return { skewness: 0, kurtosis: 0 }
  }

  const stats = computeStatistics(returns)
  return { skewness: stats.skewness, kurtosis: stats.kurtosis }
}

/**
 * 从回测结果构建图表数据集
 */
function buildChartFromResult(result: BacktestResult) {
  const { buy, sell, features } = result

  // 从买卖信号中提取价格序列
  const allPrices: { ts: number; price: number }[] = []
  for (const [, ts, , price] of buy) {
    allPrices.push({ ts, price })
  }
  for (const [, ts, , price] of sell) {
    allPrices.push({ ts, price })
  }
  allPrices.sort((a, b) => a.ts - b.ts)

  const timestamps = allPrices.map(p => p.ts)
  const prices = allPrices.map(p => p.price)

  // 计算累积回报
  const annualRet = features.annual_return ?? 0
  const dailyRet = annualRet / 252
  const dailyReturns: number[] = []
  const returnDates: number[] = []

  if (timestamps.length >= 2) {
    // 生成日收益率序列（基于回测天数）
    const startTs = timestamps[0]
    const endTs = timestamps[timestamps.length - 1]
    const days = Math.ceil((endTs - startTs) / 86400)
    const totalRet = features.total_return ?? annualRet

    // 简单线性分配（实际应由 ReportView 精确计算）
    for (let i = 0; i < days; i++) {
      dailyReturns.push(dailyRet)
      returnDates.push(startTs + i * 86400)
    }
  }

  // 计算直方图
  const stats = dailyReturns.length > 1 ? computeStatistics(dailyReturns) : null
  const distribution = stats ? {
    bins: stats.histogram.bins,
    counts: stats.histogram.counts,
    metadata: { skewness: stats.skewness, kurtosis: stats.kurtosis },
  } : null

  return buildChartData({
    priceData: prices.map((p, i) => ({ datetime: timestamps[i], close: p, price: p })),
    buySignals: buy,
    sellSignals: sell,
    dailyReturns,
    returnDates,
    drawdownSeries: undefined,
    drawdownDates: undefined,
  })
}

/**
 * 格式化指定图表数据为易读文本
 */
function formatChartData(metric: string, chartData: any): string {
  switch (metric) {
    case 'price': {
      if (!chartData.price) return "无价格数据"
      const ts = chartData.price
      const first = ts.values[0][0]
      const last = ts.values[0][ts.values[0].length - 1]
      const changePctNum = ((last - first) / first * 100)
      const changePct = changePctNum.toFixed(2)
      return [
        `**价格趋势**（${ts.timestamps.length}个数据点）`,
        `起始: ${first} → 结束: ${last}（${changePctNum > 0 ? '+' : ''}${changePct}%）`,
        `买入信号: ${ts.metadata?.buySignals?.length || 0}笔`,
        `卖出信号: ${ts.metadata?.sellSignals?.length || 0}笔`,
      ].join('\n')
    }

    case 'performance': {
      if (!chartData.performance) return "无累积回报数据"
      const ts = chartData.performance
      const lines = [`**累积回报**（${ts.timestamps.length}天）`]
      for (let i = 0; i < ts.labels.length; i++) {
        const lastVal = ts.values[i][ts.values[i].length - 1]
        lines.push(`  ${ts.labels[i]}: ${(lastVal * 100).toFixed(2)}%`)
      }
      return lines.join('\n')
    }

    case 'drawdown': {
      if (!chartData.drawdown) return "无回撤数据"
      const ts = chartData.drawdown
      const maxDD = Math.max(...ts.values[0])
      return `**回撤曲线**（${ts.timestamps.length}天）\n  最大回撤: ${(maxDD * 100).toFixed(2)}%`
    }

    case 'distribution': {
      if (!chartData.distribution) return "无分布数据"
      const d = chartData.distribution
      const skew = d.metadata?.skewness ?? 0
      const kurt = d.metadata?.kurtosis ?? 0
      return [
        `**收益率分布**（${d.bins.length}个区间）`,
        `偏度: ${skew.toFixed(4)}${skew > 0.5 ? ' (右偏)' : skew < -0.5 ? ' (左偏)' : ''}`,
        `峰度: ${kurt.toFixed(4)}${kurt > 3.5 ? ' (尖峰厚尾)' : kurt < 2.5 ? ' (低峰薄尾)' : ''}`,
      ].join('\n')
    }

    case 'dailyReturns': {
      if (!chartData.dailyReturns) return "无日收益率数据"
      const rets = chartData.dailyReturns
      const stats = computeStatistics(rets)
      return [
        `**日收益率序列**（${rets.length}天）`,
        `均值: ${(stats.mean * 100).toFixed(4)}%`,
        `标准差: ${(stats.std * 100).toFixed(4)}%`,
        `偏度: ${stats.skewness.toFixed(4)}`,
        `峰度: ${stats.kurtosis.toFixed(4)}`,
      ].join('\n')
    }

    default:
      return `未知图表类型: ${metric}。可用: price, performance, drawdown, distribution, dailyReturns`
  }
}

/**
 * 格式化回测摘要
 */
function formatBacktestSummary(result: BacktestResult): string {
  const features = result.features || {}
  const buy = result.buy || []
  const sell = result.sell || []

  const fmt = (key: string, pct = false) => {
    const v = features[key]
    if (v === undefined || v === null) return "N/A"
    if (pct) return `${(v * 100).toFixed(2)}%`
    return typeof v === 'number' && Math.abs(v) < 100 ? v.toFixed(4) : String(v)
  }

  // 如果 features 中没有 skewness/kurtosis，从信号近似计算
  let skew = features.skewness
  let kurt = features.kurtosis
  if (skew === undefined) {
    const dist = computeDistributionFeatures(buy, sell)
    skew = dist.skewness
    kurt = dist.kurtosis
  }

  const annualRet = features.annual_return ?? 0
  const sharp = features.sharp ?? 0
  const winRate = features.win_rate ?? 0

  // 简单评价
  let evaluation = ""
  if (annualRet > 0.15 && sharp > 1) {
    evaluation = "📈 策略表现优秀：年化收益高，夏普比率良好"
  } else if (annualRet > 0 && sharp > 0) {
    evaluation = "📊 策略表现一般：有正收益但风险调整后收益偏低"
  } else if (annualRet < 0) {
    evaluation = "📉 策略亏损：年化收益为负"
  } else {
    evaluation = "📊 策略表现平淡"
  }

  // 分布特征描述
  let distDesc = ""
  if (skew !== undefined && skew !== 0) {
    const skewTag = skew > 0.5 ? "正偏（右偏，极端收益较多）" :
                    skew < -0.5 ? "负偏（左偏，极端亏损较多）" :
                    "近似对称"
    const kurtTag = kurt !== undefined ?
                    (kurt > 3.5 ? "，尖峰厚尾（极端事件风险高）" :
                     kurt < 2.5 ? "，低峰薄尾（分布较平坦）" :
                     "，接近正态分布") : ""
    distDesc = `\n分布特征: 偏度 ${skew.toFixed(4)} (${skewTag})${kurtTag}`
  }

  return [
    `**【回测摘要】**`,
    `${evaluation}`,
    ``,
    `核心指标:`,
    `  年化收益 ${fmt('annual_return', true)} | 最大回撤 ${fmt('max_drawdown', true)} | 夏普 ${fmt('sharp')} | 胜率 ${fmt('win_rate', true)}`,
    `  交易次数: 买入 ${buy.length} 笔, 卖出 ${sell.length} 笔`,
    distDesc,
  ].filter(Boolean).join('\n')
}

export const backtestTool = tool(
  async (params) => {
    const { action, metric, strategyGraph } = params
    try {
      const historyStore = useHistoryStore()

      switch (action) {
        // === 执行回测 ===

        case "run_backtest": {
          if (!strategyGraph) {
            return "错误：run_backtest 需要提供 strategyGraph 参数（策略图 JSON）"
          }

          // 调用后端回测 API
          const axios = await import('axios')
          const response = await axios.default.post('/v0/backtest', {
            script: JSON.stringify(strategyGraph)
          })

          const result = response.data
          const features = result.features || {}
          const summary = result.summary || {}
          const buy = result.buy || []
          const sell = result.sell || []

          // 如果后端未返回 skewness/kurtosis，从信号近似计算
          if (features.skewness === undefined) {
            const dist = computeDistributionFeatures(buy, sell)
            features.skewness = dist.skewness
            features.kurtosis = dist.kurtosis
          }

          // 构建回测结果对象
          const backtestResult: BacktestResult = {
            backtestTime: new Date().toISOString(),
            features,
            summary,
            buy,
            sell,
          }

          // 保存到 history store（自动更新 latestBacktestVersionId）
          // 注意：如果没有 versionId，我们创建一个临时版本
          const tempVersionId = `backtest_${Date.now()}`
          await historyStore.saveBacktestResult(tempVersionId, backtestResult)

          return [
            `**【回测完成】**`,
            ``,
            formatBacktestSummary(backtestResult),
            ``,
            `完整指标数据可使用 action="get_metrics" 查询`,
            `交易信号明细可使用 action="get_signals" 查询`,
          ].join('\n')
        }

        // === 查询指标 ===

        case "get_metrics": {
          const result = await historyStore.getLatestBacktestResult()
          if (!result) return "当前没有回测结果，请先使用 action='run_backtest' 执行回测"

          return formatMetrics(result.features)
        }

        case "get_metric_value": {
          if (!metric) return "错误：get_metric_value 需要提供 metric 参数（指标名称）"
          const result = await historyStore.getLatestBacktestResult()
          if (!result) return "当前没有回测结果"

          const value = result.features?.[metric]
          if (value === undefined) return `未找到指标 "${metric}"。可用指标: ${Object.keys(result.features || {}).join(', ')}`

          const pctMetrics = ['total_return', 'annual_return', 'max_drawdown', 'volatility', 'win_rate', 'VAR', 'ES']
          const formatted = pctMetrics.includes(metric) ? `${(value * 100).toFixed(4)}%` : value.toFixed(4)
          return `${metric} = ${formatted}`
        }

        // === 查询交易信号 ===

        case "get_signals": {
          const result = await historyStore.getLatestBacktestResult()
          if (!result) return "当前没有回测结果"

          return formatSignals(result.buy || [], result.sell || [])
        }

        // === 回测摘要 ===

        case "get_summary": {
          const result = await historyStore.getLatestBacktestResult()
          if (!result) return "当前没有回测结果"

          return formatBacktestSummary(result)
        }

        // === 获取图表数据 ===

        case "get_chart_data": {
          const result = await historyStore.getLatestBacktestResult()
          if (!result) return "当前没有回测结果"

          const chartData = buildChartFromResult(result)

          if (!metric || metric === 'all') {
            // 返回所有可用图表概要
            const lines = ['可用图表数据：']
            if (chartData.price) lines.push(`- **price**: 价格趋势（${chartData.price.timestamps.length}个数据点，${chartData.price.metadata?.buySignals?.length || 0}笔买入，${chartData.price.metadata?.sellSignals?.length || 0}笔卖出）`)
            if (chartData.performance) lines.push(`- **performance**: 累积回报（${chartData.performance.timestamps.length}天，${chartData.performance.labels.join(' vs ')}）`)
            if (chartData.drawdown) lines.push(`- **drawdown**: 回撤曲线（${chartData.drawdown.timestamps.length}天）`)
            if (chartData.distribution) lines.push(`- **distribution**: 收益率分布直方图（${chartData.distribution.bins.length}个区间）`)
            if (chartData.dailyReturns) lines.push(`- **dailyReturns**: 日收益率序列（${chartData.dailyReturns.length}天）`)
            lines.push('')
            lines.push('使用 action="get_chart_data" + metric="performance/drawdown/distribution" 获取详细数据')
            return lines.join('\n')
          }

          // 返回指定图表的详细内容
          return formatChartData(metric, chartData)
        }

        default:
          return `未知 action: ${action}。支持的 action: run_backtest, get_metrics, get_metric_value, get_signals, get_summary, get_chart_data`
      }
    } catch (error: any) {
      console.warn("[Backtest Tool] 执行失败:", error)
      const msg = error.response?.data?.error || error.message || '未知错误'
      return `回测执行失败: ${msg}`
    }
  },
  {
    name: "backtest",
    description: "策略回测工具。执行回测并获取指标、交易信号、摘要等信息。action='run_backtest' 执行回测（strategyGraph=策略图JSON）；action='get_metrics' 获取完整指标列表；action='get_metric_value' 获取单个指标值（metric=指标名）；action='get_signals' 获取交易信号明细；action='get_summary' 获取回测摘要",
    schema: z.object({
      action: z.enum([
        "run_backtest",
        "get_metrics",
        "get_metric_value",
        "get_signals",
        "get_summary",
        "get_chart_data",
      ]).describe("操作类型"),
      metric: z.string().optional().describe("指标名称（get_metric_value 时需要），或图表类型（get_chart_data 时需要：price/performance/drawdown/distribution/dailyReturns/all）"),
      strategyGraph: z.any().optional().describe("策略图 JSON 数据（run_backtest 时需要）"),
    }),
  }
)
