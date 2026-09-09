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
import { applyAuthHeader } from "./common/auth"

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

      const noResultMsg =
        "当前没有回测结果。请先在主界面手动触发回测（流程图编辑器 → 回测按钮），完成后 Agent 才能读取结果分析。"

      switch (action) {
        // === 执行回测 ===

        case "run_backtest": {
          // D3-A Phase 5 决策：Agent 不直接触发后端回测。
          // 回测耗时 30 秒-数分钟，且需 UI 监控；改为提示用户手动执行。
          return [
            `**回测请手动执行**`,
            ``,
            `为避免长任务阻塞对话（回测可能耗时 30 秒-数分钟），Agent 不直接调用 \`/v0/backtest\`。`,
            ``,
            `请按以下步骤操作：`,
            `1. 在主界面打开策略流程图编辑器`,
            `2. 点击工具栏的「回测」按钮手动触发回测`,
            `3. 回测完成后，结果会自动保存到 historyStore`,
            ``,
            `完成后，Agent 可通过以下 action 读取回测结果进行分析：`,
            `- \`get_summary\` 获取回测摘要（年化/Sharpe/MaxDD/胜率）`,
            `- \`get_metrics\` 获取完整指标列表`,
            `- \`get_metric_value\` 获取单个指标值（metric=指标名）`,
            `- \`get_signals\` 获取交易信号明细`,
            `- \`get_chart_data\` 获取图表数据（price/performance/drawdown/distribution）`,
            `- \`query_performance\` 查询策略绩效（策略级，调 \`/v0/strategy/performance\`）`,
          ].join('\n')
        }

        // === 查询指标 ===

        case "get_metrics": {
          const result = await historyStore.getLatestBacktestResult()
          if (!result) return "当前没有回测结果。请先在主界面手动触发回测（流程图编辑器 → 回测按钮），完成后 Agent 才能读取结果分析。"

          return formatMetrics(result.features)
        }

        case "get_metric_value": {
          if (!metric) return "错误：get_metric_value 需要提供 metric 参数（指标名称）"
          const result = await historyStore.getLatestBacktestResult()
          if (!result) return noResultMsg

          const value = result.features?.[metric]
          if (value === undefined) return `未找到指标 "${metric}"。可用指标: ${Object.keys(result.features || {}).join(', ')}`

          const pctMetrics = ['total_return', 'annual_return', 'max_drawdown', 'volatility', 'win_rate', 'VAR', 'ES']
          const formatted = pctMetrics.includes(metric) ? `${(value * 100).toFixed(4)}%` : value.toFixed(4)
          return `${metric} = ${formatted}`
        }

        // === 查询交易信号 ===

        case "get_signals": {
          const result = await historyStore.getLatestBacktestResult()
          if (!result) return noResultMsg

          return formatSignals(result.buy || [], result.sell || [])
        }

        // === 回测摘要 ===

        case "get_summary": {
          const result = await historyStore.getLatestBacktestResult()
          if (!result) return noResultMsg

          return formatBacktestSummary(result)
        }

        // === 获取图表数据 ===

        case "get_chart_data": {
          const result = await historyStore.getLatestBacktestResult()
          if (!result) return noResultMsg

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

        // === 容量扫描 ===

        case "run_capacity_scan": {
          const { strategy, capital_min, capital_max, steps, impact_eta, closing_liquidity_ratio } = params
          if (!strategy) return "错误：run_capacity_scan 需要提供 strategy 参数（策略图 JSON）"

          const axios = await import('axios')
          applyAuthHeader(axios.default)
          const response = await axios.default.post('/v0/capacity', {
            strategy,
            capital_min: capital_min ?? 100000,
            capital_max: capital_max ?? 10000000,
            steps: steps ?? 10,
            impact_eta: impact_eta ?? 0.1,
            closing_liquidity_ratio: closing_liquidity_ratio ?? 0.05,
          })

          const d = response.data
          if (!d) return "容量扫描无结果"

          const lines = [`**容量扫描完成** — ${d.results?.length ?? 0} 个资金点`]
          lines.push(`  资金范围: ${capital_min ?? 100000} ~ ${capital_max ?? 10000000}（${steps ?? 10} 档）`)
          lines.push(`  冲击系数 η=${impact_eta ?? 0.1}，收盘流动性占比=${closing_liquidity_ratio ?? 0.05}`)

          // 容量结果（基准回测）
          if (d.capacity_metrics) {
            const cm = d.capacity_metrics
            lines.push("\n**基准回测**：")
            if (cm.sharpe !== undefined) lines.push(`  Sharpe: ${cm.sharpe.toFixed(4)}`)
            if (cm.total_return !== undefined) lines.push(`  Total Return: ${(cm.total_return * 100).toFixed(2)}%`)
            if (cm.max_drawdown !== undefined) lines.push(`  Max Drawdown: ${(cm.max_drawdown * 100).toFixed(2)}%`)
          }

          // 容量曲线（每个资金量下的指标）
          if (d.results && Array.isArray(d.results)) {
            lines.push("\n**容量曲线**（前 10 个资金点）：")
            for (const r of d.results.slice(0, 10)) {
              const capital = r.capital ?? r.amount ?? "N/A"
              const sharp = r.sharpe !== undefined ? `Sharpe=${r.sharpe.toFixed(3)}` : ""
              const ret = r.total_return !== undefined ? `Return=${(r.total_return * 100).toFixed(1)}%` : ""
              const dd = r.max_drawdown !== undefined ? `MaxDD=${(r.max_drawdown * 100).toFixed(1)}%` : ""
              lines.push(`  ${capital}: ${sharp}  ${ret}  ${dd}`)
            }
            if (d.results.length > 10) lines.push(`  ... 共 ${d.results.length} 个`)
          }

          // 容量阈值
          if (d.sharpe_decay_20 !== undefined || d.sharpe_decay_50 !== undefined) {
            lines.push("\n**容量衰减阈值**：")
            if (d.sharpe_decay_20 !== undefined) lines.push(`  Sharpe 衰减 20%: ${d.sharpe_decay_20}`)
            if (d.sharpe_decay_50 !== undefined) lines.push(`  Sharpe 衰减 50%: ${d.sharpe_decay_50}`)
          }

          return lines.join('\n')
        }

        // === 策略绩效 ===

        case "query_performance": {
          const { strategy } = params
          if (!strategy) return "错误：query_performance 需要提供 strategy 参数（策略名）"

          const axios = await import('axios')
          applyAuthHeader(axios.default)
          const response = await axios.default.get(`/v0/strategy/performance`, {
            params: { id: strategy },
          })

          const d = response.data
          if (!d) return `策略 ${strategy} 无绩效数据`

          const lines = [`**策略绩效** — ${strategy}`]
          const pctKeys = ['annual_return', 'total_return', 'max_drawdown', 'win_rate']
          for (const [k, v] of Object.entries(d as Record<string, any>)) {
            if (typeof v !== 'number') continue
            const formatted = pctKeys.includes(k)
              ? `${(v * 100).toFixed(2)}%`
              : (Math.abs(v) < 100 ? v.toFixed(4) : String(v))
            lines.push(`  ${k}: ${formatted}`)
          }
          return lines.join('\n')
        }

        default:
          return `未知 action: ${action}。支持的 action: run_backtest, get_metrics, get_metric_value, get_signals, get_summary, get_chart_data, run_capacity_scan, query_performance`
      }
    } catch (error: any) {
      console.warn("[Backtest Tool] 执行失败:", error)
      const msg = error.response?.data?.error || error.message || '未知错误'
      return `回测执行失败: ${msg}`
    }
  },
  {
    name: "backtest",
    description: "策略回测结果分析工具（D3-A Phase 5：Agent 不直接触发回测）。action='run_backtest' 提示用户手动触发回测（返回操作指引，不调用 /v0/backtest）；action='get_metrics' 获取完整指标列表；action='get_metric_value' 获取单个指标值（metric=指标名）；action='get_signals' 获取交易信号明细；action='get_summary' 获取回测摘要；action='get_chart_data' 获取图表数据（price/performance/drawdown/distribution/dailyReturns）；action='run_capacity_scan' 容量扫描（strategy=策略图JSON）；action='query_performance' 查询策略绩效（strategy=策略名或ID）。用户手动回测后，Agent 通过 get_* 类 action 读取 historyStore 中的结果进行分析。",
    schema: z.object({
      action: z.enum([
        "run_backtest",
        "get_metrics",
        "get_metric_value",
        "get_signals",
        "get_summary",
        "get_chart_data",
        "run_capacity_scan",
        "query_performance",
      ]).describe("操作类型"),
      metric: z.string().optional().describe("指标名称（get_metric_value 时需要），或图表类型（get_chart_data 时需要：price/performance/drawdown/distribution/dailyReturns/all）"),
      strategyGraph: z.any().optional().describe("策略图 JSON 数据（已废弃：run_backtest 不再调用后端，保留仅为向后兼容）"),
      strategy: z.union([z.string(), z.number()]).optional().describe("策略名（query_performance）或策略图 JSON（run_capacity_scan）"),
      capital_min: z.number().optional().describe("容量扫描最小资金量（默认 100000）"),
      capital_max: z.number().optional().describe("容量扫描最大资金量（默认 10000000）"),
      steps: z.number().optional().describe("容量扫描资金分档数（默认 10）"),
      impact_eta: z.number().optional().describe("平方根冲击模型系数（默认 0.1）"),
      closing_liquidity_ratio: z.number().optional().describe("收盘流动性占比（默认 0.05）"),
    }),
  }
)
