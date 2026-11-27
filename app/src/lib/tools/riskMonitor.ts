/**
 * 风控监控 Tool
 *
 * Agent 可通过此工具：
 * - 查询所有策略风险指标（IR / CUSUM / VaR / MaxDD / Sharpe）
 * - 查询资金风控配置（止损线 / 单日限额 / 当前权益）
 * - 查询单日亏损风控配置
 * - 查询风控断路器状态
 * - 查询所有止损设置
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

export const riskMonitorTool = tool(
  async (params) => {
    if (!getAuthToken()) {
      return "错误：未登录或 token 已过期。请先在登录界面完成登录。"
    }
    const { action } = params
    const axios = await getAxios()

    try {
      switch (action) {

        case "query_strategies": {
          const res = await axios.get(`${BASE_URL}/risk/strategies`)
          const items = res.data
          if (!items || (Array.isArray(items) && items.length === 0)) {
            return "暂无策略风险指标（策略系统未初始化或无运行策略）"
          }

          const list = Array.isArray(items) ? items : Object.values(items)
          const lines = [`**所有策略风险指标** — 共 ${list.length} 个策略`]
          for (const item of list.slice(0, 30)) {
            const name = (item as any).name ?? (item as any).strategy ?? (item as any).strategy_id ?? "?"
            const i = item as any
            const fields: [string, string][] = []
            if (i.information_ratio !== undefined) fields.push(["IR", i.information_ratio.toFixed(4)])
            if (i.sharpe !== undefined) fields.push(["Sharpe", i.sharpe.toFixed(4)])
            if (i.max_drawdown !== undefined) fields.push(["MaxDD", `${(i.max_drawdown * 100).toFixed(2)}%`])
            if (i.var !== undefined) fields.push(["VaR", `${(i.var * 100).toFixed(2)}%`])
            if (i.cusum_signal !== undefined) fields.push(["CUSUM", i.cusum_signal.toFixed(4)])
            if (i.win_rate !== undefined) fields.push(["WinRate", `${(i.win_rate * 100).toFixed(2)}%`])
            if (i.health !== undefined) fields.push(["Health", String(i.health)])
            const summary = fields.map(([k, v]) => `${k}=${v}`).join(", ")
            lines.push(`  ${name}: ${summary}`)
          }
          if (list.length > 30) lines.push(`  ... 共 ${list.length} 个，仅显示前 30 个`)
          return lines.join("\n")
        }

        case "query_capital": {
          const res = await axios.get(`${BASE_URL}/risk/capital`)
          const d = res.data
          if (!d) return "无资金风控配置"

          const lines = [`**资金风控配置与状态**`]
          // 兼容多种字段命名
          const knownKeys = ['stoploss_line', 'daily_limit', 'current_equity', 'total_capital', 'used_capital', 'available', 'max_drawdown']
          if (d.stoploss_line !== undefined) lines.push(`  止损线: ${d.stoploss_line}`)
          if (d.daily_limit !== undefined) lines.push(`  单日限额: ${d.daily_limit}`)
          if (d.current_equity !== undefined) lines.push(`  当前权益: ${d.current_equity}`)
          if (d.total_capital !== undefined) lines.push(`  总资金: ${d.total_capital}`)
          if (d.used_capital !== undefined) lines.push(`  已用资金: ${d.used_capital}`)
          if (d.available !== undefined) lines.push(`  可用资金: ${d.available}`)
          if (d.max_drawdown !== undefined) lines.push(`  最大回撤: ${formatPercent(d.max_drawdown)}`)
          for (const [k, v] of Object.entries(d as Record<string, any>)) {
            if (knownKeys.includes(k)) continue
            lines.push(`  ${k}: ${typeof v === 'number' ? v.toFixed(4) : JSON.stringify(v).slice(0, 100)}`)
          }
          return lines.join("\n")
        }

        case "query_daily": {
          const res = await axios.get(`${BASE_URL}/risk/daily`)
          const d = res.data
          if (!d) return "无单日亏损风控配置"

          const lines = [`**单日亏损风控配置与状态**`]
          const knownKeys = ['daily_loss_limit', 'current_daily_loss', 'is_triggered', 'threshold']
          if (d.daily_loss_limit !== undefined) lines.push(`  单日亏损限额: ${d.daily_loss_limit}`)
          if (d.current_daily_loss !== undefined) lines.push(`  当前单日亏损: ${d.current_daily_loss}`)
          if (d.is_triggered !== undefined) lines.push(`  是否触发: ${d.is_triggered ? "是" : "否"}`)
          if (d.threshold !== undefined) lines.push(`  阈值: ${d.threshold}`)
          for (const [k, v] of Object.entries(d as Record<string, any>)) {
            if (knownKeys.includes(k)) continue
            lines.push(`  ${k}: ${typeof v === 'number' ? v.toFixed(4) : JSON.stringify(v).slice(0, 100)}`)
          }
          return lines.join("\n")
        }

        case "query_status": {
          const res = await axios.get(`${BASE_URL}/risk/status`)
          const d = res.data
          if (!d) return "无风控状态数据"

          const lines = [`**风控断路器状态**`]
          for (const [k, v] of Object.entries(d as Record<string, any>)) {
            if (typeof v === 'object' && v !== null && !Array.isArray(v)) {
              lines.push(`  ${k}:`)
              for (const [kk, vv] of Object.entries(v)) {
                lines.push(`    ${kk}: ${typeof vv === 'number' ? formatNumber(vv as number) : vv}`)
              }
            } else if (Array.isArray(v)) {
              lines.push(`  ${k}: [${v.slice(0, 10).join(", ")}${v.length > 10 ? `, ... 共 ${v.length} 项` : ""}]`)
            } else {
              lines.push(`  ${k}: ${typeof v === 'number' ? formatNumber(v) : v}`)
            }
          }
          return lines.join("\n")
        }

        case "query_stoploss": {
          const res = await axios.get(`${BASE_URL}/risk/stoploss`)
          const items = res.data
          if (!items || (Array.isArray(items) && items.length === 0)) {
            return "暂无止损设置"
          }

          const list = Array.isArray(items) ? items : Object.values(items)
          const lines = [`**所有止损设置** — 共 ${list.length} 条`]
          for (const item of list.slice(0, 30)) {
            const i = item as any
            const fields: string[] = []
            if (i.symbol !== undefined) fields.push(`symbol=${i.symbol}`)
            if (i.threshold !== undefined) fields.push(`threshold=${i.threshold}`)
            if (i.percentage !== undefined) fields.push(`pct=${formatPercent(i.percentage)}`)
            if (i.type !== undefined) fields.push(`type=${i.type}`)
            if (i.is_triggered !== undefined) fields.push(`triggered=${i.is_triggered}`)
            if (i.id !== undefined) fields.push(`id=${i.id}`)
            lines.push(`  ${fields.join(", ")}`)
          }
          if (list.length > 30) lines.push(`  ... 共 ${list.length} 条，仅显示前 30 条`)
          return lines.join("\n")
        }

        default:
          return `未知 action: ${action}`
      }
    } catch (error: any) {
      console.warn("[Risk Monitor Tool] 执行失败:", error)
      const msg = error.response?.data?.error || error.message || "未知错误"
      return `风控查询失败: ${msg}`
    }
  },
  {
    name: "risk_monitor",
    description: [
      '风控监控工具：只读查询策略风险指标、资金风控配置、单日亏损风控、断路器状态、止损设置。本工具不做任何写操作或配置修改。',
      '',
      '## WHEN TO USE',
      '- 用户要“横向对比所有策略的风险指标”（IR/CUSUM/VaR/MaxDD/Sharpe/WinRate/Health）',
      '- 用户要查“风控视角的资金配置”（止损线/单日限额/当前权益/总资金/已用/可用）',
      '- 用户要查“单日亏损风控是否触发”',
      '- 用户要查“风控断路器状态”',
      '- 用户要查“所有止损设置”',
      '',
      '## WHEN NOT TO USE',
      '- 用户要点名查“某一个策略的详细绩效指标” → 用 performance.query_strategy_performance',
      '- 用户要查“交易账户可用资金/能买多少” → 用 order_query.query_funds',
      '- 用户要“修改止损线/调整限额/关闭断路器” → 本工具只读，应说明无法修改并提示走配置界面',
      '- 用户要“查持仓/查订单” → 用 order_query',
      '',
      '## DO NOT USE FOR（跨工具互斥）',
      '- 不要用 query_strategies 回答“某策略 Sharpe 多少” → 那用 performance.query_strategy_performance',
      '- 不要用 query_capital 回答“我现在能买多少” → 那用 order_query.query_funds',
      '- 不要把 query_capital 与 query_daily 混为一谈：前者是资金层风控，后者是单日亏损层风控；用户问“风控配置”时，如不确定哪一层，两个都查并分段呈现',
      '',
      '## ACTION 选择规则',
      '- query_strategies：全策略横向风险指标列表。用于“哪些策略风险高/对比所有策略”',
      '- query_capital：资金风控配置与状态（止损线/单日限额/当前权益/总资金/已用/可用/最大回撤）',
      '- query_daily：单日亏损风控配置与状态（限额/当前亏损/是否触发/阈值）',
      '- query_status：断路器状态（结构化对象，含嵌套字段）',
      '- query_stoploss：所有止损设置列表（symbol/threshold/percentage/type/triggered）',
      '',
      '## EXAMPLES',
      '- “看看所有策略的风险指标” → query_strategies',
      '- “现在的止损线和单日限额是多少” → query_capital',
      '- “今天单日亏损触发风控了吗” → query_daily',
      '- “风控断路器现在什么状态” → query_status',
      '- “茅台设了哪些止损” → query_stoploss',
      '- “茅台这个策略 Sharpe 多少” → NOT this tool → performance.query_strategy_performance',
      '- “我账户里还有多少钱可买” → NOT this tool → order_query.query_funds',
    ].join('\n'),
    schema: z.object({
      action: z.enum([
        "query_strategies", "query_capital", "query_daily", "query_status", "query_stoploss"
      ]).describe("操作类型"),
    }),
  }
)