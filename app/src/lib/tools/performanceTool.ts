/**
 * 绩效分析 Tool
 *
 * Agent 可通过此工具：
 * - 查询策略绩效指标（Sharpe / MaxDD / AnnualReturn / WinRate 等）
 * - 前端内置计算单标的绩效（基于历史价格计算 Sharpe / Vol / MaxDD / WinRate）
 */

import { tool } from "@langchain/core/tools"
import { z } from "zod"
import { applyAuthHeader, getAuthToken } from "./common/auth"

const BASE_URL = "/v0"

async function getAxios() {
  const mod = await import("axios")
  const axios = mod.default
  applyAuthHeader(axios)
  return axios
}

function formatNumber(v: number | undefined, digits = 4): string {
  if (v === undefined || v === null || isNaN(v)) return "N/A"
  return typeof v === "number" && Math.abs(v) < 1000 ? v.toFixed(digits) : String(v)
}

function formatPercent(v: number | undefined, digits = 2): string {
  if (v === undefined || v === null || isNaN(v)) return "N/A"
  return `${(v * 100).toFixed(digits)}%`
}

/**
 * 前端计算 Sharpe Ratio（基于日收益率，年化）
 */
function computeSharpe(returns: number[], riskFreeDaily = 0): number {
  if (returns.length < 2) return NaN
  const excess = returns.map(r => r - riskFreeDaily)
  const mean = excess.reduce((a, b) => a + b, 0) / excess.length
  const variance = excess.reduce((acc, v) => acc + (v - mean) ** 2, 0) / (excess.length - 1)
  const std = Math.sqrt(variance)
  if (std === 0) return NaN
  return (mean / std) * Math.sqrt(252) // 年化
}

/**
 * 前端计算年化波动率
 */
function computeAnnualVolatility(returns: number[]): number {
  if (returns.length < 2) return NaN
  const mean = returns.reduce((a, b) => a + b, 0) / returns.length
  const variance = returns.reduce((acc, v) => acc + (v - mean) ** 2, 0) / (returns.length - 1)
  return Math.sqrt(variance) * Math.sqrt(252)
}

/**
 * 前端计算最大回撤
 */
function computeMaxDrawdown(prices: number[]): number {
  if (prices.length < 2) return NaN
  let peak = prices[0]
  let maxDD = 0
  for (const p of prices) {
    if (p > peak) peak = p
    const dd = (peak - p) / peak
    if (dd > maxDD) maxDD = dd
  }
  return maxDD
}

/**
 * 前端计算胜率（上涨日数占比）
 */
function computeWinRate(returns: number[]): number {
  if (returns.length === 0) return NaN
  const wins = returns.filter(r => r > 0).length
  return wins / returns.length
}

/**
 * 从价格序列提取数据（兼容不同字段命名）
 */
function extractPrices(data: any[]): number[] {
  return data
    .map((bar: any) => bar.close ?? bar.Close ?? bar.c ?? bar.price ?? bar.value)
    .filter((p: number | undefined) => typeof p === 'number' && !isNaN(p))
}

/**
 * 从价格序列提取日期
 */
function extractDates(data: any[]): (string | number)[] {
  return data.map((bar: any) => bar.datetime ?? bar.date ?? bar.Date ?? bar.time ?? bar.timestamp ?? "")
}

export const performanceTool = tool(
  async (params) => {
    if (!getAuthToken()) {
      return "错误：未登录或 token 已过期。请先在登录界面完成登录。"
    }
    const { action } = params
    const axios = await getAxios()

    try {
      switch (action) {

        case "query_strategy_performance": {
          const { strategy } = params
          if (!strategy) return "错误：query_strategy_performance 需要 strategy 参数（策略名或 ID）"

          const res = await axios.get(`${BASE_URL}/strategy/performance`, {
            params: { id: strategy },
          })
          const d = res.data
          if (!d) return `策略 ${strategy} 无绩效数据`

          const lines = [`**策略绩效** — ${strategy}`]
          const pctKeys = ['annual_return', 'total_return', 'max_drawdown', 'win_rate']
          for (const [k, v] of Object.entries(d as Record<string, any>)) {
            if (typeof v !== 'number') {
              // 字符串字段也展示（如策略名称等）
              lines.push(`  ${k}: ${v}`)
              continue
            }
            const formatted = pctKeys.includes(k)
              ? `${(v * 100).toFixed(2)}%`
              : (Math.abs(v) < 100 ? v.toFixed(4) : String(v))
            lines.push(`  ${k}: ${formatted}`)
          }
          return lines.join("\n")
        }

        case "compute_symbol_metrics": {
          const { symbol, start_date, end_date, freq, table } = params
          if (!symbol) return "错误：compute_symbol_metrics 需要 symbol 参数"

          // 拉取历史行情
          const res = await axios.get(`${BASE_URL}/quote`, {
            params: {
              table: table ?? "stock_1d",
              symbol,
              ...(start_date ? { start: start_date } : {}),
              ...(end_date ? { end: end_date } : {}),
              limit: 5000,
            },
          })

          const data = res.data?.data ?? []
          if (data.length < 2) return `${symbol} 数据不足（${data.length} 条），无法计算指标`

          const prices = extractPrices(data)
          if (prices.length < 2) return `${symbol} 价格数据缺失，无法计算指标`

          const dates = extractDates(data)
          const firstDate = dates[0]
          const lastDate = dates[dates.length - 1]
          const days = prices.length

          // 计算日收益率
          const returns: number[] = []
          for (let i = 1; i < prices.length; i++) {
            if (prices[i - 1] === 0) continue
            returns.push((prices[i] - prices[i - 1]) / prices[i - 1])
          }

          const sharpe = computeSharpe(returns)
          const annualVol = computeAnnualVolatility(returns)
          const maxDD = computeMaxDrawdown(prices)
          const winRate = computeWinRate(returns)
          const totalReturn = (prices[prices.length - 1] - prices[0]) / prices[0]

          const lines = [
            `**${symbol} 绩效指标** — ${days} 个交易日 (${firstDate} → ${lastDate})`,
            `  频率: ${freq ?? "daily"}`,
            `  价格区间: ${formatNumber(prices[0], 2)} → ${formatNumber(prices[prices.length - 1], 2)}（总收益 ${(totalReturn * 100).toFixed(2)}%）`,
            ``,
            `  **Sharpe Ratio**（年化）: ${formatNumber(sharpe)}`,
            `  年化波动率: ${formatPercent(annualVol)}`,
            `  最大回撤: ${formatPercent(maxDD)}`,
            `  胜率: ${formatPercent(winRate)}`,
            `  有效日收益样本: ${returns.length} 个`,
          ]
          return lines.join("\n")
        }

        default:
          return `未知 action: ${action}`
      }
    } catch (error: any) {
      console.warn("[Performance Tool] 执行失败:", error)
      const msg = error.response?.data?.error || error.message || "未知错误"
      return `绩效分析失败: ${msg}`
    }
  },
  {
    name: "performance",
    description: [
      "绩效分析工具。",
      "action='query_strategy_performance' 查询策略绩效指标（strategy=策略名或 ID）；",
      "action='compute_symbol_metrics' 前端计算单标的绩效指标（symbol=标的代码，start_date/end_date 时间范围，freq=daily/5m/15m 默认 daily），返回 Sharpe/Vol/MaxDD/WinRate/总收益",
    ].join(""),
    schema: z.object({
      action: z.enum(["query_strategy_performance", "compute_symbol_metrics"]).describe("操作类型"),
      strategy: z.union([z.string(), z.number()]).optional().describe("策略名或 ID（query_strategy_performance）"),
      table: z.string().optional().describe("行情表名（compute_symbol_metrics，默认 stock_1d）"),
      symbol: z.string().optional().describe("标的代码（compute_symbol_metrics，如 sh.600000）"),
      start_date: z.string().optional().describe("起始日期 YYYY-MM-DD（compute_symbol_metrics）"),
      end_date: z.string().optional().describe("结束日期 YYYY-MM-DD（compute_symbol_metrics）"),
      freq: z.string().optional().describe("数据频率: daily/5m/15m/30m/60m（默认 daily）"),
    }),
  }
)