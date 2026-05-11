/**
 * 计算器 Tool
 *
 * Agent 在涉及精确计算时调用此工具：
 * - 基础算术：加减乘除、括号、幂运算
 * - 百分比计算
 * - 复利/单利计算
 * - 对数、平方根
 * - 汇率换算（USD/CNY/EUR/JPY/HKD/GBP）
 * - 期权组合收益计算（单一期权、价差、跨式、宽跨式、比例价差等）
 */

import { tool } from "@langchain/core/tools"
import { z } from "zod"

// ============================================================
// 汇率表（近似近期汇率，实盘场景应接实时 API）
// ============================================================
const FX_RATES: Record<string, number> = {
  "USD/CNY": 7.245, "CNY/USD": 1 / 7.245,
  "EUR/CNY": 7.852, "CNY/EUR": 1 / 7.852,
  "HKD/CNY": 0.928, "CNY/HKD": 1 / 0.928,
  "JPY/CNY": 7.245 / 150.5, "CNY/JPY": 150.5,
  "GBP/CNY": 9.15,  "CNY/GBP": 1 / 9.15,
  "USD/EUR": 0.923, "EUR/USD": 1.083,
  "USD/JPY": 150.5, "JPY/USD": 1 / 150.5,
  "USD/HKD": 7.81,  "HKD/USD": 1 / 7.81,
  "GBP/USD": 1.265, "USD/GBP": 1 / 1.265,
}

function lookupRate(from: string, to: string): number | null {
  const key = `${from.toUpperCase()}/${to.toUpperCase()}`
  return FX_RATES[key] ?? null
}

export const calculatorTool = tool(
  async ({ action, expression, amount, fromCurrency, toCurrency, originalValue, newValue, principal, rate, periods, base, exponent, value, optionStrategy, spotPrice, strike, premium, quantity, direction, strike2, premium2, quantity2, strike3, premium3, quantity3, ratio, targetPrice }) => {
    try {
      switch (action) {
        // === 表达式求值 ===

        case "eval": {
          if (!expression) return "错误：需要提供 expression 参数，如 '2 + 3 * (4 - 1)'"
          const sanitized = expression.replace(/\s/g, "")
          if (!/^[\d+\-*/().%^eE]+$/.test(sanitized)) {
            return "错误：表达式包含非法字符。仅支持数字和 + - * / ( ) ^ % 运算符"
          }
          try {
            const jsExpr = sanitized.replace(/\^/g, "**")
            const result = Function(`"use strict"; return (${jsExpr})`)()
            return `计算结果: ${expression} = ${fmt(result)}`
          } catch (e: any) {
            return `表达式计算失败: ${e.message}`
          }
        }

        // === 百分比 ===

        case "percent": {
          if (amount === undefined || rate === undefined) return "错误：需要 amount（基数）和 rate（百分比）"
          return `${amount} 的 ${rate}% = ${fmt(amount * rate / 100)}`
        }

        case "percent_of": {
          if (amount === undefined || newValue === undefined) return "错误：需要 amount（分子）和 newValue（分母）"
          if (newValue === 0) return "错误：分母不能为 0"
          return `${amount} 是 ${newValue} 的 ${fmt(amount / newValue * 100)}%`
        }

        case "percent_change": {
          if (originalValue === undefined || newValue === undefined) return "错误：需要 originalValue（原值）和 newValue（新值）"
          if (originalValue === 0) return "错误：原值不能为 0"
          const change = (newValue - originalValue) / Math.abs(originalValue) * 100
          const dir = change > 0 ? "增长" : change < 0 ? "下降" : "无变化"
          return `${originalValue} → ${newValue}，${dir} ${fmt(Math.abs(change))}%`
        }

        // === 利息计算 ===

        case "compound": {
          if (principal === undefined || rate === undefined || periods === undefined) {
            return "错误：需要 principal（本金）、rate（利率）、periods（期数）"
          }
          const finalValue = principal * Math.pow(1 + rate, periods)
          return `复利: 本金 ${fmt(principal)} × (1 + ${rate})^${periods} = ${fmt(finalValue)}（收益 ${fmt(finalValue - principal)}）`
        }

        case "simple_interest": {
          if (principal === undefined || rate === undefined || periods === undefined) {
            return "错误：需要 principal（本金）、rate（利率）、periods（期数）"
          }
          const interest = principal * rate * periods
          return `单利: 本金 ${fmt(principal)} × ${rate} × ${periods} = 利息 ${fmt(interest)}，合计 ${fmt(principal + interest)}`
        }

        // === 幂运算 / 对数 / 根号 ===

        case "pow": {
          if (base === undefined || exponent === undefined) return "错误：需要 base（底数）和 exponent（指数）"
          return `${base} ^ ${exponent} = ${fmt(Math.pow(base, exponent))}`
        }

        case "sqrt": {
          if (value === undefined || value < 0) return "错误：需要 value（非负数）"
          return `√${value} = ${fmt(Math.sqrt(value))}`
        }

        case "log": {
          if (value === undefined || value <= 0) return "错误：需要 value（正数）"
          return `ln(${value}) = ${fmt(Math.log(value))} | log10(${value}) = ${fmt(Math.log10(value))}`
        }

        // === 汇率换算 ===

        case "convert": {
          if (amount === undefined || !fromCurrency || !toCurrency) return "错误：需要 amount（金额）、fromCurrency（源货币）、toCurrency（目标货币）"
          const rate = lookupRate(fromCurrency, toCurrency)
          if (!rate) {
            const available = [...new Set(Object.keys(FX_RATES).flatMap(k => k.split("/")))].sort().join(", ")
            return `未找到 ${fromCurrency}/${toCurrency} 汇率。可用货币: ${available}`
          }
          return `${amount} ${fromCurrency} = ${fmt(amount * rate)} ${toCurrency}（汇率: 1 ${fromCurrency} = ${fmt(rate)} ${toCurrency}）`
        }

        // === 绝对值 ===

        case "abs": {
          if (value === undefined) return "错误：需要 value 参数"
          return `|${value}| = ${Math.abs(value)}`
        }

        // === 期权组合收益计算 ===

        case "option_payoff": {
          if (!optionStrategy) return "错误：需要提供 optionStrategy（期权策略类型）"
          return calculateOptionPayoff({
            strategy: optionStrategy,
            spotPrice,
            strike,
            premium,
            quantity,
            direction,
            strike2,
            premium2,
            quantity2,
            strike3,
            premium3,
            quantity3,
            ratio,
            targetPrice,
          })
        }

        default:
          return `未知操作: ${action}。支持: eval, percent, percent_of, percent_change, compound, simple_interest, pow, sqrt, log, convert, abs, option_payoff`
      }
    } catch (error: any) {
      return `计算失败: ${error.message}`
    }
  },
  {
    name: "calculator",
    description: "精确计算工具。支持基础算术、百分比、复利/单利、幂/对数/根号、汇率换算、绝对值、期权组合收益计算。action='eval' 表达式求值（expression='2+3*4'）；action='percent' 百分比（amount=基数, rate=百分比）；action='percent_of' 占比（amount=分子, to=分母）；action='percent_change' 涨跌幅（from=原值, to=新值）；action='compound' 复利（principal=本金, rate=利率, periods=期数）；action='simple_interest' 单利；action='pow' 幂运算（base=底数, exponent=指数）；action='sqrt' 平方根（value=数值）；action='log' 对数（value=正数）；action='convert' 汇率换算（amount=金额, from/to=货币代码如USD/CNY）；action='abs' 绝对值；action='option_payoff' 期权收益（optionStrategy=策略类型, spotPrice=标的价格, strike=行权价, premium=权利金, quantity=数量, direction=买卖方向）",
    schema: z.object({
      action: z.enum([
        "eval", "percent", "percent_of", "percent_change",
        "compound", "simple_interest",
        "pow", "sqrt", "log",
        "convert", "abs",
        "option_payoff",
      ]).describe("操作类型"),
      expression: z.string().optional().describe("算术表达式（eval 时需要），如 '2 + 3 * (4 - 1)'"),
      amount: z.number().optional().describe("数值（percent/convert 时需要）"),
      fromCurrency: z.string().optional().describe("源货币代码（convert 需要），如 USD, CNY, EUR"),
      toCurrency: z.string().optional().describe("目标货币代码（convert 需要）"),
      originalValue: z.number().optional().describe("原值（percent_change 需要）"),
      newValue: z.number().optional().describe("新值（percent_change/percent_of 需要）"),
      principal: z.number().optional().describe("本金（compound/simple_interest 需要）"),
      rate: z.number().optional().describe("利率（percent 时为百分比值，compound/simple_interest 时为小数如 0.05）"),
      periods: z.number().optional().describe("期数（compound/simple_interest 需要）"),
      base: z.number().optional().describe("底数（pow 需要）"),
      exponent: z.number().optional().describe("指数（pow 需要）"),
      value: z.number().optional().describe("数值（sqrt/log/abs 需要）"),
      // 期权组合参数
      optionStrategy: z.enum([
        "long_call", "long_put", "short_call", "short_put",
        "bull_call_spread", "bear_put_spread",
        "straddle", "strangle",
        "butterfly", "condor",
        "ratio_spread", "calendar_spread",
      ]).optional().describe("期权策略类型"),
      spotPrice: z.number().optional().describe("标的资产当前价格"),
      strike: z.number().optional().describe("行权价（第一条腿）"),
      premium: z.number().optional().describe("权利金（第一条腿，正数表示支付，负数表示收取）"),
      quantity: z.number().optional().describe("合约数量（第一条腿）"),
      direction: z.enum(["buy", "sell"]).optional().describe("买卖方向（第一条腿）"),
      strike2: z.number().optional().describe("行权价（第二条腿）"),
      premium2: z.number().optional().describe("权利金（第二条腿）"),
      quantity2: z.number().optional().describe("合约数量（第二条腿）"),
      strike3: z.number().optional().describe("行权价（第三条腿，butterfly 需要）"),
      premium3: z.number().optional().describe("权利金（第三条腿）"),
      quantity3: z.number().optional().describe("合约数量（第三条腿）"),
      ratio: z.number().optional().describe("比例（ratio_spread 需要，如 2 表示 1:2）"),
      targetPrice: z.number().optional().describe("目标价格（计算该价格下的收益）"),
    }),
  }
)

/**
 * 格式化数字
 */
function fmt(n: number): string {
  if (!isFinite(n)) return String(n)
  if (Math.abs(n) >= 1e9 || (Math.abs(n) < 0.0001 && n !== 0)) {
    return n.toExponential(4)
  }
  // 保留足够精度，去掉尾部零
  return parseFloat(n.toFixed(6)).toString()
}

// ============================================================
// 期权组合收益计算
// ============================================================

interface OptionPayoffParams {
  strategy: string
  spotPrice?: number
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

/**
 * 计算单一期权在到期时的收益
 * @param S 标的资产价格
 * @param K 行权价
 * @param P 权利金（正值=支付，负值=收取）
 * @param type 'call' | 'put'
 * @param direction 'buy' | 'sell'
 * @param qty 合约数量
 * @returns 收益（正=盈利，负=亏损）
 */
function singleOptionPayoff(S: number, K: number, P: number, type: "call" | "put", direction: "buy" | "sell", qty: number = 1): number {
  const intrinsic = type === "call" ? Math.max(S - K, 0) : Math.max(K - S, 0)
  const payoff = direction === "buy" ? intrinsic - P : P - intrinsic
  return payoff * qty
}

/**
 * 计算期权组合收益
 */
function calculateOptionPayoff(params: OptionPayoffParams): string {
  const {
    strategy, spotPrice, strike, premium, quantity, direction,
    strike2, premium2, quantity2, strike3, premium3, quantity3,
    ratio, targetPrice
  } = params

  // 验证必需参数
  if (strike === undefined || premium === undefined) {
    return "错误：期权计算需要 strike（行权价）和 premium（权利金）参数"
  }

  const S = spotPrice ?? strike // 默认使用行权价作为当前价
  const qty = quantity ?? 1
  const dir = direction ?? "buy"

  // 计算不同价格点的收益
  const prices = targetPrice ? [targetPrice] : generatePricePoints(S, strike, strike2, strike3)
  
  let results: string[]
  
  switch (strategy) {
    case "long_call":
      results = prices.map(p => {
        const payoff = singleOptionPayoff(p, strike!, premium!, "call", "buy", qty)
        return `价格 ${p}: 收益 = ${fmt(payoff)}`
      })
      break

    case "long_put":
      results = prices.map(p => {
        const payoff = singleOptionPayoff(p, strike!, premium!, "put", "buy", qty)
        return `价格 ${p}: 收益 = ${fmt(payoff)}`
      })
      break

    case "short_call":
      results = prices.map(p => {
        const payoff = singleOptionPayoff(p, strike!, premium!, "call", "sell", qty)
        return `价格 ${p}: 收益 = ${fmt(payoff)}`
      })
      break

    case "short_put":
      results = prices.map(p => {
        const payoff = singleOptionPayoff(p, strike!, premium!, "put", "sell", qty)
        return `价格 ${p}: 收益 = ${fmt(payoff)}`
      })
      break

    case "bull_call_spread": {
      // 牛市看涨价差：买入低行权价 call + 卖出高行权价 call
      if (!strike2 || premium2 === undefined) return "错误：价差需要 strike2 和 premium2"
      const q2 = quantity2 ?? qty
      results = prices.map(p => {
        const leg1 = singleOptionPayoff(p, strike!, premium!, "call", "buy", qty)
        const leg2 = singleOptionPayoff(p, strike2!, premium2!, "call", "sell", q2)
        return `价格 ${p}: 收益 = ${fmt(leg1 + leg2)}`
      })
      break
    }

    case "bear_put_spread": {
      // 熊市看跌价差：买入高行权价 put + 卖出低行权价 put
      if (!strike2 || premium2 === undefined) return "错误：价差需要 strike2 和 premium2"
      const q2 = quantity2 ?? qty
      results = prices.map(p => {
        const leg1 = singleOptionPayoff(p, strike!, premium!, "put", "buy", qty)
        const leg2 = singleOptionPayoff(p, strike2!, premium2!, "put", "sell", q2)
        return `价格 ${p}: 收益 = ${fmt(leg1 + leg2)}`
      })
      break
    }

    case "straddle": {
      // 跨式：买入相同行权价的 call + put
      if (premium2 === undefined) return "错误：跨式需要 premium2（put 权利金）"
      const q2 = quantity2 ?? qty
      results = prices.map(p => {
        const callPayoff = singleOptionPayoff(p, strike!, premium!, "call", "buy", qty)
        const putPayoff = singleOptionPayoff(p, strike!, premium2!, "put", "buy", q2)
        return `价格 ${p}: 收益 = ${fmt(callPayoff + putPayoff)}`
      })
      break
    }

    case "strangle": {
      // 宽跨式：买入不同行权价的 call + put
      if (!strike2 || premium2 === undefined) return "错误：宽跨式需要 strike2 和 premium2"
      const q2 = quantity2 ?? qty
      results = prices.map(p => {
        const callPayoff = singleOptionPayoff(p, strike!, premium!, "call", "buy", qty)
        const putPayoff = singleOptionPayoff(p, strike2!, premium2!, "put", "buy", q2)
        return `价格 ${p}: 收益 = ${fmt(callPayoff + putPayoff)}`
      })
      break
    }

    case "butterfly": {
      // 蝶式：买入低行权价 call + 卖出 2×中间行权价 call + 买入高行权价 call
      if (!strike2 || !strike3 || premium2 === undefined || premium3 === undefined) {
        return "错误：蝶式需要 strike2, strike3, premium2, premium3"
      }
      const q2 = quantity2 ?? qty * 2
      const q3 = quantity3 ?? qty
      results = prices.map(p => {
        const leg1 = singleOptionPayoff(p, strike!, premium!, "call", "buy", qty)
        const leg2 = singleOptionPayoff(p, strike2!, premium2!, "call", "sell", q2)
        const leg3 = singleOptionPayoff(p, strike3!, premium3!, "call", "buy", q3)
        return `价格 ${p}: 收益 = ${fmt(leg1 + leg2 + leg3)}`
      })
      break
    }

    case "condor": {
      // 鹰式：买入低行权价 call + 卖出中间低行权价 call + 卖出中间高行权价 call + 买入高行权价 call
      if (!strike2 || !strike3 || premium2 === undefined || premium3 === undefined) {
        return "错误：鹰式需要 strike2, strike3, premium2, premium3"
      }
      const q2 = quantity2 ?? qty
      const q3 = quantity3 ?? qty
      const q4 = quantity3 ?? qty // 第四条腿
      const premium4 = premium3 // 简化：使用相同权利金
      results = prices.map(p => {
        const leg1 = singleOptionPayoff(p, strike!, premium!, "call", "buy", qty)
        const leg2 = singleOptionPayoff(p, strike2!, premium2!, "call", "sell", q2)
        const leg3 = singleOptionPayoff(p, strike2! + (strike3! - strike2!) / 2, premium2!, "call", "sell", q3)
        const leg4 = singleOptionPayoff(p, strike3!, premium4, "call", "buy", q4)
        return `价格 ${p}: 收益 = ${fmt(leg1 + leg2 + leg3 + leg4)}`
      })
      break
    }

    case "ratio_spread": {
      // 比例价差：买入 1 份 + 卖出 ratio 份
      if (!ratio || !strike2 || premium2 === undefined) {
        return "错误：比例价差需要 ratio（比例）, strike2, premium2"
      }
      const sellQty = qty * ratio
      results = prices.map(p => {
        const leg1 = singleOptionPayoff(p, strike!, premium!, "call", "buy", qty)
        const leg2 = singleOptionPayoff(p, strike2!, premium2!, "call", "sell", sellQty)
        return `价格 ${p}: 收益 = ${fmt(leg1 + leg2)}`
      })
      break
    }

    case "calendar_spread": {
      // 日历价差：卖出近月 + 买入远月（简化计算，假设相同行权价）
      if (!strike2 || premium2 === undefined) return "错误：日历价差需要 strike2 和 premium2"
      const q2 = quantity2 ?? qty
      results = prices.map(p => {
        // 简化：假设近月期权到期，远月仍有时间价值
        const leg1 = singleOptionPayoff(p, strike!, premium!, "call", "sell", qty)
        const leg2 = singleOptionPayoff(p, strike2!, premium2!, "call", "buy", q2)
        return `价格 ${p}: 收益 = ${fmt(leg1 + leg2)}（简化计算，未考虑时间价值衰减）`
      })
      break
    }

    default:
      return `未知期权策略: ${strategy}。支持: long_call, long_put, short_call, short_put, bull_call_spread, bear_put_spread, straddle, strangle, butterfly, condor, ratio_spread, calendar_spread`
  }

  // 构建返回结果
  const breakeven = calculateBreakeven(strategy, params)
  const maxProfit = calculateMaxProfit(strategy, params)
  const maxLoss = calculateMaxLoss(strategy, params)

  return [
    `期权组合收益计算 [${strategy}]`,
    `当前价格: ${S}, 合约乘数: ${qty}`,
    "",
    ...results,
    "",
    breakeven ? `盈亏平衡点: ${breakeven}` : "盈亏平衡点: 不适用",
    maxProfit !== null ? `最大盈利: ${maxProfit}` : "最大盈利: 无限",
    maxLoss !== null ? `最大亏损: ${maxLoss}` : "最大亏损: 无限",
  ].join("\n")
}

/**
 * 生成关键价格点
 */
function generatePricePoints(spot: number, k1: number, k2?: number, k3?: number): number[] {
  const strikes = [k1, k2, k3].filter(Boolean).sort((a, b) => a! - b!) as number[]
  const minK = Math.min(...strikes, spot)
  const maxK = Math.max(...strikes, spot)
  const range = maxK - minK
  const step = range * 0.25 || spot * 0.05

  return [
    minK - step * 2,
    minK - step,
    minK,
    ...strikes,
    maxK + step,
    maxK + step * 2,
  ].map(p => parseFloat(p.toFixed(2)))
}

/**
 * 计算盈亏平衡点
 */
function calculateBreakeven(strategy: string, params: OptionPayoffParams): string | null {
  const { strike, premium, strike2, premium2 } = params

  switch (strategy) {
    case "long_call":
      return strike! + premium! !== undefined ? fmt(strike! + premium!) : null
    case "long_put":
      return strike! - premium! !== undefined ? fmt(strike! - premium!) : null
    case "short_call":
      return strike! + premium! !== undefined ? fmt(strike! + premium!) : null
    case "short_put":
      return strike! - premium! !== undefined ? fmt(strike! - premium!) : null
    case "bull_call_spread":
    case "bear_put_spread":
      if (strike2 !== undefined && premium2 !== undefined) {
        const netPremium = premium! + premium2!
        return fmt(strike! + netPremium)
      }
      return null
    case "straddle":
      if (premium2 !== undefined) {
        const totalPremium = premium! + premium2!
        return `${fmt(strike! - totalPremium)} / ${fmt(strike! + totalPremium)}`
      }
      return null
    case "strangle":
      if (strike2 !== undefined && premium2 !== undefined) {
        const totalPremium = premium! + premium2!
        return `${fmt(strike2! - totalPremium)} / ${fmt(strike! + totalPremium)}`
      }
      return null
    default:
      return null
  }
}

/**
 * 计算最大盈利
 */
function calculateMaxProfit(strategy: string, params: OptionPayoffParams): string | null {
  const { strike, premium, strike2, premium2, ratio } = params

  switch (strategy) {
    case "long_call":
    case "long_put":
      return null // 理论上无限
    case "short_call":
      return fmt(premium!) // 最大盈利 = 权利金
    case "short_put":
      return fmt(premium!)
    case "bull_call_spread":
      if (strike2 !== undefined && premium2 !== undefined) {
        const netPremium = premium! + premium2!
        return fmt(strike2! - strike! + netPremium)
      }
      return null
    case "bear_put_spread":
      if (strike2 !== undefined && premium2 !== undefined) {
        const netPremium = premium! + premium2!
        return fmt(strike! - strike2! + netPremium)
      }
      return null
    case "straddle":
      if (premium2 !== undefined) {
        return null // 理论上无限
      }
      return null
    case "strangle":
      return null // 理论上无限
    case "butterfly":
      if (strike2 !== undefined && strike2 !== undefined && premium2 !== undefined) {
        // 简化计算
        return fmt((strike2! - strike!) - premium! - premium2! - (premium2! || 0))
      }
      return null
    case "ratio_spread":
      if (ratio && strike2 !== undefined && premium2 !== undefined) {
        return fmt(strike2! - strike! + premium! + ratio * premium2!)
      }
      return null
    default:
      return null
  }
}

/**
 * 计算最大亏损
 */
function calculateMaxLoss(strategy: string, params: OptionPayoffParams): string | null {
  const { strike, premium, strike2, premium2, ratio } = params

  switch (strategy) {
    case "long_call":
      return fmt(-premium!) // 最大亏损 = 支付的权利金
    case "long_put":
      return fmt(-premium!)
    case "short_call":
      return null // 理论上无限
    case "short_put":
      return fmt(-(strike! - premium!)) // 标的跌到 0
    case "bull_call_spread":
      if (strike2 !== undefined && premium2 !== undefined) {
        const netPremium = premium! + premium2!
        return fmt(-netPremium)
      }
      return null
    case "bear_put_spread":
      if (strike2 !== undefined && premium2 !== undefined) {
        const netPremium = premium! + premium2!
        return fmt(-netPremium)
      }
      return null
    case "straddle":
      if (premium2 !== undefined) {
        return fmt(-(premium! + premium2!))
      }
      return null
    case "strangle":
      if (premium2 !== undefined) {
        return fmt(-(premium! + premium2!))
      }
      return null
    case "butterfly":
      if (strike2 !== undefined && premium2 !== undefined) {
        return fmt(-(premium! + premium2! + (premium2! || 0)))
      }
      return null
    case "ratio_spread":
      if (ratio && strike2 !== undefined && premium2 !== undefined) {
        return null // 理论上无限
      }
      return null
    default:
      return null
  }
}
