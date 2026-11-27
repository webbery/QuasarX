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

// === 组合收益（payoff）===
//
// 收益图是纯本地计算（不需要后端），放在这里而不是 javascript 沙箱：
// 期权相关的能力归期权域管，模型不需要自己手搓腿数与乘数。

type PayoffLeg = { type: "call" | "put"; direction: "buy" | "sell"; strike: number; premium: number; quantity: number }

interface PayoffSpec {
  optionStrategy?: string
  spot?: number
  strike?: number
  premium?: number
  quantity?: number
  direction?: "buy" | "sell"
  strike2?: number
  premium2?: number
  quantity2?: number
  strike3?: number
  premium3?: number
  quantity3?: number
  ratio?: number
  targetPrice?: number
}

function singleLegPayoff(S: number, leg: PayoffLeg): number {
  const intrinsic = leg.type === "call" ? Math.max(S - leg.strike, 0) : Math.max(leg.strike - S, 0)
  return (leg.direction === "buy" ? intrinsic - leg.premium : leg.premium - intrinsic) * leg.quantity
}

/** 把策略描述展开成腿 */
function buildLegs(spec: PayoffSpec): { legs: PayoffLeg[]; error?: string } {
  const { optionStrategy: strategy, strike, premium } = spec
  const qty = spec.quantity ?? 1
  const dir = spec.direction ?? "buy"
  const k2 = spec.strike2
  const p2 = spec.premium2
  const q2 = spec.quantity2 ?? qty
  const k3 = spec.strike3
  const p3 = spec.premium3
  const q3 = spec.quantity3 ?? qty
  const ratio = spec.ratio ?? 1

  if (strike === undefined || premium === undefined) {
    return { legs: [], error: "payoff 需要 strike（行权价）和 premium（权利金）参数" }
  }

  const leg = (type: "call" | "put", direction: "buy" | "sell", K: any, P: any, q: any): PayoffLeg | null => {
    if (typeof K !== "number" || typeof P !== "number") return null
    return { type, direction, strike: K, premium: P, quantity: q ?? 1 }
  }

  const legs: PayoffLeg[] = []
  const add = (...ls: Array<PayoffLeg | null>) => {
    for (const l of ls) if (l) legs.push(l)
  }

  switch (strategy) {
    case "long_call": add(leg("call", "buy", strike, premium, qty)); break
    case "long_put": add(leg("put", "buy", strike, premium, qty)); break
    case "short_call": add(leg("call", "sell", strike, premium, qty)); break
    case "short_put": add(leg("put", "sell", strike, premium, qty)); break
    case "bull_call_spread":
      add(leg("call", "buy", strike, premium, qty), leg("call", "sell", k2, p2, q2)); break
    case "bear_put_spread":
      add(leg("put", "buy", strike, premium, qty), leg("put", "sell", k2, p2, q2)); break
    case "straddle":
      add(leg("call", "buy", strike, premium, qty), leg("put", "buy", strike, p2, q2)); break
    case "strangle":
      add(leg("call", "buy", strike, premium, qty), leg("put", "buy", k2, p2, q2)); break
    case "butterfly":
      add(
        leg("call", "buy", strike, premium, qty),
        leg("call", "sell", k2, p2, qty * 2),
        leg("call", "buy", k3, p3, q3),
      ); break
    case "condor":
      add(
        leg("call", "buy", strike, premium, qty),
        leg("call", "sell", k2, p2, qty),
        leg("call", "sell", typeof k2 === "number" && typeof k3 === "number" ? (k2 + k3) / 2 : undefined, p3, qty),
        leg("call", "buy", k3, p3, qty),
      ); break
    case "ratio_spread":
      add(leg("call", "buy", strike, premium, qty), leg("call", "sell", k2, p2, qty * ratio)); break
    case "calendar_spread":
      // 简化：近月卖出、远月买入，行权价相同
      add(leg("call", "sell", strike, premium, qty), leg("call", "buy", strike, p2, q2)); break
    default:
      return {
        legs: [],
        error: `未知期权策略: ${strategy}。支持: long_call, long_put, short_call, short_put, bull_call_spread, bear_put_spread, straddle, strangle, butterfly, condor, ratio_spread, calendar_spread`,
      }
  }
  if (legs.length === 0) return { legs: [], error: "策略参数不足，无法构建腿组合" }
  return { legs }
}

/** 计算并格式化组合收益文本 */
function optionPayoffText(spec: PayoffSpec): string {
  const { legs, error } = buildLegs(spec)
  if (error) return error

  const S = spec.spot ?? spec.strike!
  const qty = spec.quantity ?? 1

  // 价格扫描点：行权价与现价附近
  const strikes = legs.map((l) => l.strike)
  const minK = Math.min(...strikes, S)
  const maxK = Math.max(...strikes, S)
  const range = maxK - minK
  const step = range * 0.25 || S * 0.05 || 1
  const prices = [
    minK - step * 2, minK - step, minK,
    ...strikes,
    maxK + step, maxK + step * 2,
  ].map((p) => Number(p.toFixed(2)))

  const payoffs = prices.map((price) => ({
    price,
    payoff: legs.reduce((sum, leg) => sum + singleLegPayoff(price, leg), 0),
  }))

  // 盈亏平衡点：收益符号变化处（线性插值）
  const breakeven: number[] = []
  for (let i = 1; i < payoffs.length; i++) {
    const a = payoffs[i - 1]
    const b = payoffs[i]
    if (a.payoff === 0 || (a.payoff < 0 && b.payoff > 0) || (a.payoff > 0 && b.payoff < 0)) {
      const t = b.payoff === a.payoff ? 0.5 : (0 - a.payoff) / (b.payoff - a.payoff)
      breakeven.push(Number((a.price + t * (b.price - a.price)).toFixed(2)))
    }
  }

  const maxProfit = Math.max(...payoffs.map((p) => p.payoff))
  const maxLoss = Math.min(...payoffs.map((p) => p.payoff))
  const fmtP = (n: number) => (Math.abs(n) >= 1e9 || (Math.abs(n) < 0.0001 && n !== 0) ? n.toExponential(4) : String(Number(n.toFixed(4))))

  const lines = [
    `期权组合收益 [${spec.optionStrategy}]`,
    `当前价格: ${S}，行权价: ${strikes.join(" / ")}`,
    "",
    ...payoffs.map((p) => `  价格 ${p.price}: 收益 = ${fmtP(p.payoff)}`),
    "",
    breakeven.length ? `盈亏平衡点: ${breakeven.join(" / ")}` : "盈亏平衡点: 扫描范围内无符号变化",
    `最大盈利: ${fmtP(maxProfit)}`,
    `最大亏损: ${fmtP(maxLoss)}`,
  ]
  if (spec.optionStrategy === "calendar_spread") lines.push("（日历价差为简化计算，未考虑时间价值衰减）")
  return lines.join("\n")
}

export const optionPricingTool = tool(
  async (params) => {
    const { action } = params

    // payoff 是纯本地计算，不需要登录、不调后端 —— 放在鉴权之前
    if (action === "payoff") {
      return optionPayoffText(params as unknown as PayoffSpec)
    }

    if (!getAuthToken()) {
      return "错误：未登录或 token 已过期。请先在登录界面完成登录。"
    }
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
      '期权定价、IV 曲面与组合收益分析工具。',
      '',
      '## WHEN TO USE',
      '- 用户要求给单个期权合约定价（BSM/MC/二叉树）→ pricing',
      '- 用户要求给同一标的多个行权价批量定价 → pricing_multi',
      '- 用户要求查询交易所 IV 曲面 → iv_surface',
      '- 用户要求画/算期权组合收益图（盈亏平衡点、最大盈亏）→ payoff',
      '',
      '## WHEN NOT TO USE',
      '- 用户要“查期权合约列表/期权历史行情/期权合约详情” → 用数据管理工具（query_option_simple/query_option_history/query_option_data），本工具不做行情查询',
      '- 用户要“查某标的的价格/波动率输入” → 用数据管理工具取数，再回来定价',
      '- 用户只问“什么是 Delta”等概念 → 不调用',
      '',
      '## DO NOT USE FOR（跨工具互斥）',
      '- 不要用本工具查期权行情或合约列表 → 数据管理工具',
      '- 不要在 payoff 里传 method / is_call → payoff 只用 optionStrategy 与腿参数',
      '- 不要在 pricing 里传 optionStrategy / premium / strike2 → 单合约只用 spot/strike/expiry_days/volatility/is_call',
      '',
      '## ACTION 分组与选择规则',
      '',
      '### A. 单合约定价（需登录）',
      '- pricing：必填 spot/strike/expiry_days/volatility；is_call 默认 true；method=black_scholes/monte_carlo/binomial（默认 black_scholes）',
      '- pricing_multi：必填 spot/expiry_days/volatility/contracts（数组，每项 {strike, is_call?, expiry?, volatility?}）。不要给 pricing_multi 传顶层 strike',
      '',
      '### B. IV 曲面（需登录）',
      '- iv_surface：必填 exchange=CFFEX/SSE/SZSE，product=品种（如 510050/510300/50ETF/300ETF）',
      '',
      '### C. 组合收益（本地计算，无需登录）',
      '- payoff：必填 optionStrategy、strike、premium；spot 默认取 strike；quantity 默认 1',
      '- 腿参数映射：第一条腿=strike/premium/quantity/direction；第二条腿=strike2/premium2/quantity2；第三条腿=strike3/premium3/quantity3；ratio 仅 ratio_spread 用',
      '- 策略与腿数对应：long_call/long_put/short_call/short_put=1 腿；bull_call_spread/bear_put_spread/straddle/strangle/ratio_spread/calendar_spread=2 腿；butterfly=3 腿（中间腿数量自动×2）；condor=4 腿（用 strike/strike2/strike3 三条显式 K）',
      '',
      '## 参数一参多义提示',
      '- strike：pricing 中是行权价；payoff 中是第一条腿的行权价',
      '- is_call：仅 pricing/pricing_multi 使用，表示单合约看涨/看跌',
      '- optionStrategy：仅 payoff 使用，用 long_call/short_put 等表达方向+类型，不要与 is_call 混用',
      '- premium：仅 payoff 使用；正数表示支付权利金，负数表示收取',
      '',
      '## EXAMPLES',
      '- “给 3 个月后到期、行权价 100、现价 105、波动率 20% 的看涨期权定价” → pricing(spot=105, strike=100, expiry_days=90, volatility=0.2, is_call=true)',
      '- “给 510050 多个行权价批量定价” → pricing_multi(spot=..., expiry_days=..., volatility=..., contracts=[{strike:2.5},{strike:2.6}])',
      '- “查 510050 的 IV 曲面” → iv_surface(exchange="SSE", product="510050")',
      '- “画一个牛市看涨价差的收益图” → payoff(optionStrategy="bull_call_spread", spot=..., strike=..., premium=..., strike2=..., premium2=...)',
      '- “买入看涨期权” → 若要定价走 pricing(is_call=true)；若要收益图走 payoff(optionStrategy="long_call")',
    ].join('\n'),
    schema: z.object({
      action: z.enum(["pricing", "pricing_multi", "iv_surface", "payoff"]).describe("操作类型"),
      spot: z.number().optional().describe("标的价格（pricing/pricing_multi 必填；payoff 时作为现价）"),
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
      // === payoff（组合收益）参数 ===
      optionStrategy: z.enum([
        "long_call", "long_put", "short_call", "short_put",
        "bull_call_spread", "bear_put_spread",
        "straddle", "strangle",
        "butterfly", "condor",
        "ratio_spread", "calendar_spread",
      ]).optional().describe("期权策略类型（payoff 必填）"),
      premium: z.number().optional().describe("权利金（第一条腿，正数表示支付，负数表示收取）"),
      quantity: z.number().optional().describe("合约数量（第一条腿，默认 1）"),
      direction: z.enum(["buy", "sell"]).optional().describe("买卖方向（第一条腿，默认 buy）"),
      strike2: z.number().optional().describe("行权价（第二条腿）"),
      premium2: z.number().optional().describe("权利金（第二条腿）"),
      quantity2: z.number().optional().describe("合约数量（第二条腿）"),
      strike3: z.number().optional().describe("行权价（第三条腿，butterfly 需要）"),
      premium3: z.number().optional().describe("权利金（第三条腿）"),
      quantity3: z.number().optional().describe("合约数量（第三条腿）"),
      ratio: z.number().optional().describe("比例（ratio_spread 需要，如 2 表示 1:2）"),
    }),
  }
)