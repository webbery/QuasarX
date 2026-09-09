/**
 * 期权定价 Tool
 *
 * Agent 可通过此工具：
 * - 单合约定价（BSM / Monte Carlo / Binomial Tree）
 * - 批量定价同一标的多个 strike
 * - 查询交易所期权 IV 曲面
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

export const optionPricingTool = tool(
  async (params) => {
    if (!getAuthToken()) {
      return "错误：未登录或 token 已过期。请先在登录界面完成登录。"
    }
    const { action } = params
    const axios = await getAxios()

    try {
      switch (action) {

        case "pricing": {
          const { spot, strike, expiry_days, volatility, risk_free_rate, dividend_yield, is_call, method } = params
          if (spot === undefined || strike === undefined || expiry_days === undefined || volatility === undefined) {
            return "错误：pricing 需要 spot、strike、expiry_days、volatility 参数"
          }

          const res = await axios.post(`${BASE_URL}/option/pricing`, {
            method: method ?? "black_scholes",
            spot,
            strike,
            T: expiry_days / 365,
            volatility,
            risk_free_rate: risk_free_rate ?? 0.015,
            dividend_yield: dividend_yield ?? 0,
            is_call: is_call ?? true,
            is_american: false,
            n_paths: 10000,
            n_steps: 100,
          })
          const d = res.data
          if (!d) return "期权定价无结果"

          const lines = [`**期权定价** (${method ?? "black_scholes"})`]
          lines.push(`  Spot: ${spot}, Strike: ${strike}, T: ${expiry_days}天, σ: ${volatility}`)
          lines.push(`  类型: ${is_call ?? true ? "Call" : "Put"}`)
          lines.push(`  **价格: ${formatNumber(d.price)}**`)
          lines.push(`  内在价值: ${formatNumber(d.intrinsic_value ?? d.intrinsic)}`)
          lines.push(`  时间价值: ${formatNumber(d.time_value)}`)
          if (d.moneyness) lines.push(`  价值状态: ${d.moneyness}`)

          if (d.greeks) {
            lines.push("\n**Greeks**：")
            lines.push(`  Delta: ${formatNumber(d.greeks.delta)}`)
            lines.push(`  Gamma: ${formatNumber(d.greeks.gamma, 6)}`)
            lines.push(`  Theta: ${formatNumber(d.greeks.theta)}`)
            lines.push(`  Vega: ${formatNumber(d.greeks.vega)}`)
            lines.push(`  Rho: ${formatNumber(d.greeks.rho)}`)
          }

          if (d.mc_std_error !== undefined) {
            lines.push(`\nMC 标准误差: ${formatNumber(d.mc_std_error, 6)}`)
          }
          if (d.early_exercise_premium !== undefined) {
            lines.push(`提前行权溢价: ${formatNumber(d.early_exercise_premium)}`)
          }

          // 收益曲线（前 5 个点）
          if (d.payoff_curve && Array.isArray(d.payoff_curve) && d.payoff_curve.length > 0) {
            lines.push("\n**收益曲线**（前 5 点）：")
            for (const p of d.payoff_curve.slice(0, 5)) {
              lines.push(`  Spot ${formatNumber(p.spot, 2)} → 到期收益 ${formatNumber(p.payoff_at_expiry, 2)}, 当前价值 ${formatNumber(p.payoff_now, 2)}`)
            }
          }

          return lines.join("\n")
        }

        case "pricing_multi": {
          const { spot, contracts, expiry_days, volatility, risk_free_rate, dividend_yield, is_call, method } = params
          if (spot === undefined || !contracts || contracts.length === 0) {
            return "错误：pricing_multi 需要 spot 和 contracts 参数（contracts=[{strike},...]）"
          }
          if (expiry_days === undefined || volatility === undefined) {
            return "错误：pricing_multi 需要 expiry_days 和 volatility 参数"
          }

          const res = await axios.post(`${BASE_URL}/option/pricing_multi`, {
            method: method ?? "black_scholes",
            spot,
            expiry_days,
            T: expiry_days / 365,
            volatility,
            risk_free_rate: risk_free_rate ?? 0.015,
            dividend_yield: dividend_yield ?? 0,
            is_call: is_call ?? true,
            is_american: false,
            n_paths: 10000,
            n_steps: 100,
            contracts,
          })
          const d = res.data
          // 响应可能是数组或包含 results 字段
          const results = Array.isArray(d) ? d : (d?.results ?? d?.data ?? [])
          if (results.length === 0) return "批量定价无结果"

          const lines = [`**批量期权定价** — ${contracts.length} 个合约`]
          lines.push(`  Spot: ${spot}, T: ${expiry_days}天, σ: ${volatility}\n`)

          for (const c of results.slice(0, 30)) {
            const tag = c.is_call !== undefined ? (c.is_call ? "Call" : "Put") : (is_call ?? true ? "Call" : "Put")
            const greeks = c.greeks?.delta !== undefined ? `, Delta=${formatNumber(c.greeks.delta)}` : ""
            lines.push(`  Strike ${c.strike} (${tag}): 价格=${formatNumber(c.price)}${greeks}`)
          }
          if (results.length > 30) lines.push(`  ... 共 ${results.length} 个，仅显示前 30 个`)
          return lines.join("\n")
        }

        case "iv_surface": {
          const { exchange, product } = params
          if (!exchange || !product) return "错误：iv_surface 需要 exchange 和 product 参数"

          const res = await axios.get(`${BASE_URL}/option/iv_surface`, {
            params: { exchange, product },
          })
          const d = res.data
          if (!d) return "IV 曲面无数据"

          const lines = [`**IV 曲面** — ${exchange} ${product}`]
          const rawPoints = d.raw_points ?? []
          lines.push(`  原始数据点: ${rawPoints.length}`)

          if (d.strikes && d.expiry_days) {
            lines.push(`  Strike 范围: ${d.strikes[0]} ~ ${d.strikes[d.strikes.length - 1]}（${d.strikes.length} 个）`)
            lines.push(`  到期天数: ${d.expiry_days.join(", ")}`)
          }

          if (d.surface && Array.isArray(d.surface) && d.surface.length > 0) {
            lines.push(`  插值网格: ${d.surface.length} 行 × ${d.surface[0]?.length ?? 0} 列`)
            // 显示几个代表点（中间行均值 + 几个 ATM 点）
            const mid = Math.floor(d.surface.length / 2)
            const midRow = d.surface[mid]
            if (midRow && midRow.length > 0) {
              const avg = midRow.reduce((a: number, b: number) => a + b, 0) / midRow.length
              lines.push(`  中间到期日 IV 均值: ${(avg * 100).toFixed(2)}%`)
            }
          }

          if (rawPoints.length > 0) {
            lines.push("\n**前 5 个数据点**：")
            for (const p of rawPoints.slice(0, 5)) {
              const cp = p.call_put ? ` (${p.call_put})` : ""
              lines.push(`  Strike ${p.strike} ${p.expiry_days}天${cp}: IV=${formatNumber(p.iv)}${p.contract_name ? ` ${p.contract_name}` : ""}`)
            }
          }

          return lines.join("\n")
        }

        default:
          return `未知 action: ${action}`
      }
    } catch (error: any) {
      console.warn("[Option Pricing Tool] 执行失败:", error)
      const msg = error.response?.data?.error || error.message || "未知错误"
      return `期权定价失败: ${msg}`
    }
  },
  {
    name: "option_pricing",
    description: [
      "期权定价与 IV 曲面分析工具。",
      "action='pricing' 单合约定价（spot/strike/expiry_days/volatility 必填，method=black_scholes/monte_carlo/binomial）；",
      "action='pricing_multi' 批量定价同一标的多个 strike（contracts=[{strike,is_call?},...]）；",
      "action='iv_surface' 查询交易所期权 IV 曲面（exchange=CFFEX/SSE/SZSE，product=品种名如 510050/510300/510500）",
    ].join(""),
    schema: z.object({
      action: z.enum(["pricing", "pricing_multi", "iv_surface"]).describe("操作类型"),
      spot: z.number().optional().describe("标的价格（pricing/pricing_multi 必填）"),
      strike: z.number().optional().describe("行权价（pricing 必填）"),
      contracts: z.array(z.object({
        strike: z.number().describe("行权价"),
        is_call: z.boolean().optional().describe("是否看涨（默认 true）"),
        expiry: z.string().optional().describe("到期日 YYYY-MM-DD（可选）"),
        volatility: z.number().optional().describe("独立波动率（可选）"),
      })).optional().describe("合约列表（pricing_multi 必填）"),
      expiry_days: z.number().optional().describe("到期天数（pricing/pricing_multi 必填）"),
      volatility: z.number().optional().describe("年化波动率（pricing/pricing_multi 必填）"),
      risk_free_rate: z.number().optional().describe("无风险利率（默认 0.015）"),
      dividend_yield: z.number().optional().describe("股息率（默认 0）"),
      is_call: z.boolean().optional().describe("是否看涨（默认 true）"),
      method: z.string().optional().describe("定价方法: black_scholes/monte_carlo/binomial（默认 black_scholes）"),
      exchange: z.string().optional().describe("交易所: CFFEX/SSE/SZSE（iv_surface 必填）"),
      product: z.string().optional().describe("品种: 50ETF/300ETF/500ETF/510050/510300 等（iv_surface 必填）"),
    }),
  }
)