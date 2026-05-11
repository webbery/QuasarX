/**
 * 突变检测 Tool
 *
 * 独立工具，可扫描各类时间序列数据，检测纵向变化剧烈的位置
 * Agent 可通过多种方法扫描同一位置，逐步分析突变原因
 *
 * 使用场景：
 * - 用户: "第10天产生了突变，分析原因"
 * - Agent: 调用 mutationTool 扫描 performance/price/drawdown 曲线
 * - 获取突变的具体日期和严重程度
 * - 再调用 backtestTool/quoteTool/knowledgeTool 查找该日期的上下文
 */

import { tool } from "@langchain/core/tools"
import { z } from "zod"
import { useHistoryStore } from "@/stores/history"
import { detectAnomalies, buildChartData } from "@/lib/chartData"
import type { TimeSeries } from "@/lib/chartData"

/**
 * 从回测结果构建图表数据集（复用 backtest.ts 的逻辑）
 */
function buildChartFromResult(result: any) {
  const { buy, sell, features } = result

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

  const annualRet = features?.annual_return ?? 0
  const dailyRet = annualRet / 252
  const dailyReturns: number[] = []
  const returnDates: number[] = []

  if (timestamps.length >= 2) {
    const startTs = timestamps[0]
    const endTs = timestamps[timestamps.length - 1]
    const days = Math.ceil((endTs - startTs) / 86400)
    for (let i = 0; i < days; i++) {
      dailyReturns.push(dailyRet)
      returnDates.push(startTs + i * 86400)
    }
  }

  // 动态导入 computeStatistics
  const { computeStatistics } = require("@/lib/statistics")
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

export const mutationTool = tool(
  async (params) => {
    const { action, target, method, windowSize, threshold, minGapDays, date, toleranceDays } = params
    try {
      const historyStore = useHistoryStore()

      switch (action) {
        // === 扫描整个序列，找出所有突变点 ===

        case "detect": {
          const result = await historyStore.getLatestBacktestResult()
          if (!result) return "当前没有回测结果，无法检测突变"

          const chartData = buildChartFromResult(result)

          // 获取目标序列
          let series: TimeSeries | null = null
          if (target === 'price') series = chartData.price
          else if (target === 'performance') series = chartData.performance
          else if (target === 'drawdown') series = chartData.drawdown
          else if (target === 'dailyReturns' && chartData.dailyReturns) {
            // 日收益率序列需要包装成 TimeSeries
            const { computeStatistics } = require("@/lib/statistics")
            const stats = computeStatistics(chartData.dailyReturns)
            series = {
              timestamps: Array.from({ length: chartData.dailyReturns.length }, (_, i) =>
                (result.buy?.[0]?.[1] || Date.now() / 1000) + i * 86400
              ),
              values: [chartData.dailyReturns],
              labels: ['日收益率'],
            }
          }

          if (!series) return `无 ${target} 数据。可用序列: price, performance, drawdown, dailyReturns`

          const scanOptions = {
            method: (method || 'slope') as any,
            windowSize: windowSize || 20,
            threshold: threshold || 2.5,
            minGapDays: minGapDays || 3,
          }

          const anomalies = detectAnomalies(series, scanOptions)

          if (anomalies.length === 0) {
            return `未检测到 ${target} 序列的显著突变点。曲线波动在正常范围内。`
          }

          const lines = [`**${target} 序列突变检测**（方法: ${method || 'slope'}，阈值: ${threshold || 2.5}σ）`, '']
          for (const a of anomalies) {
            const severityIcon = a.severity === 'high' ? '🔴' : a.severity === 'medium' ? '🟡' : '🟢'
            const date = new Date(a.timestamp * 1000).toLocaleDateString('zh-CN')
            lines.push(`${severityIcon} **${date}** — ${a.context}`)
            lines.push(`   值: ${a.value.toFixed(4)} | 变化: ${(a.changePct * 100).toFixed(2)}% | Z-Score: ${a.zScore.toFixed(1)}`)
          }

          return lines.join('\n')
        }

        // === 检查特定日期附近是否有突变 ===

        case "check_date": {
          if (!date) return "错误：check_date 需要提供 date 参数（YYYY-MM-DD 或时间戳）"

          const result = await historyStore.getLatestBacktestResult()
          if (!result) return "当前没有回测结果"

          const chartData = buildChartFromResult(result)

          // 解析目标日期
          const targetDate = parseDate(date)
          if (!targetDate) return `无法解析日期: ${date}。请使用 YYYY-MM-DD 格式或 Unix 时间戳`

          const tolerance = (toleranceDays || 5) * 86400000 // 默认前后5天
          const windowStart = targetDate - tolerance
          const windowEnd = targetDate + tolerance

          const scanOptions = {
            method: (method || 'slope') as any,
            windowSize: windowSize || 20,
            threshold: threshold || 2.5,
            minGapDays: minGapDays || 1, // 检查模式降低间隔要求
          }

          const results: string[] = []

          for (const seqName of [target || 'performance']) {
            let series: TimeSeries | null = null
            if (seqName === 'price') series = chartData.price
            else if (seqName === 'performance') series = chartData.performance
            else if (seqName === 'drawdown') series = chartData.drawdown

            if (!series) continue

            const anomalies = detectAnomalies(series, scanOptions)
            const nearby = anomalies.filter(a =>
              a.timestamp * 1000 >= windowStart && a.timestamp * 1000 <= windowEnd
            )

            if (nearby.length > 0) {
              results.push(`**${seqName}** 在目标日期附近发现 ${nearby.length} 个突变点：`)
              for (const a of nearby) {
                const d = new Date(a.timestamp * 1000).toLocaleDateString('zh-CN')
                const daysDiff = Math.round((a.timestamp * 1000 - targetDate) / 86400000)
                const when = daysDiff === 0 ? '当天' : daysDiff > 0 ? `后${daysDiff}天` : `前${Math.abs(daysDiff)}天`
                results.push(`  - ${d}（${when}）: ${a.context}`)
              }
            } else {
              results.push(`**${seqName}** 在目标日期附近未检测到显著突变。`)
            }
          }

          return results.join('\n\n')
        }

        // === 列出所有可扫描的序列 ===

        case "list_targets": {
          const result = await historyStore.getLatestBacktestResult()
          if (!result) return "当前没有回测结果"

          const chartData = buildChartFromResult(result)
          const available: string[] = []
          if (chartData.price) available.push(`price（价格趋势，${chartData.price.timestamps.length}个点）`)
          if (chartData.performance) available.push(`performance（累积回报，${chartData.performance.timestamps.length}天）`)
          if (chartData.drawdown) available.push(`drawdown（回撤曲线，${chartData.drawdown.timestamps.length}天）`)
          if (chartData.dailyReturns) available.push(`dailyReturns（日收益率，${chartData.dailyReturns.length}天）`)

          return `可扫描的序列：\n${available.map(s => `- ${s}`).join('\n')}`
        }

        default:
          return `未知 action: ${action}。支持的 action: detect, check_date, list_targets`
      }
    } catch (error: any) {
      console.warn("[Mutation Tool] 执行失败:", error)
      const msg = error.response?.data?.error || error.message || '未知错误'
      return `突变检测失败: ${msg}`
    }
  },
  {
    name: "mutation",
    description: "突变检测工具。扫描时间序列数据，检测纵向变化剧烈的位置。支持逐步分析：先 detect 找突变点，再 check_date 检查特定日期附近。action='detect' 扫描整个序列（target=price/performance/drawdown/dailyReturns, method=slope/volatility/jump/reversal）；action='check_date' 检查特定日期附近是否有突变（date=YYYY-MM-DD, toleranceDays=容差天数）；action='list_targets' 列出所有可扫描的序列",
    schema: z.object({
      action: z.enum([
        "detect",
        "check_date",
        "list_targets",
      ]).describe("操作类型"),
      target: z.string().optional().describe("目标序列（detect 时需要）: price/performance/drawdown/dailyReturns，默认 performance"),
      method: z.string().optional().describe("检测方法: slope(斜率突变)/volatility(波动突变)/jump(跳跃)/reversal(趋势反转)，默认 slope"),
      windowSize: z.number().optional().describe("局部窗口大小，默认 20"),
      threshold: z.number().optional().describe("Z-Score 阈值，默认 2.5"),
      minGapDays: z.number().optional().describe("最小间隔天数，默认 3（check_date 模式自动降为 1）"),
      date: z.string().optional().describe("目标日期（check_date 时需要）: YYYY-MM-DD 或 Unix 时间戳"),
      toleranceDays: z.number().optional().describe("容差天数（check_date 时可选）: 检查目标日期前后几天，默认 5"),
    }),
  }
)

/**
 * 解析日期字符串或时间戳
 */
function parseDate(input: string): number | null {
  // 尝试解析为 Unix 时间戳（毫秒）
  const ts = Number(input)
  if (!isNaN(ts) && ts > 1e9) {
    // 如果输入是秒级时间戳，转为毫秒
    return ts < 1e12 ? ts * 1000 : ts
  }

  // 尝试解析为 YYYY-MM-DD
  const d = new Date(input)
  if (!isNaN(d.getTime())) {
    return d.getTime()
  }

  return null
}
