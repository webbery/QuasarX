/**
 * 订单查询 Tool
 *
 * Agent 可通过此工具：
 * - 查询当前持仓
 * - 查询可用资金
 * - 查询订单状态
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

export const orderQueryTool = tool(
  async (params) => {
    if (!getAuthToken()) {
      return "错误：未登录或 token 已过期。请先在登录界面完成登录。"
    }
    const { action } = params
    const axios = await getAxios()

    try {
      switch (action) {

        case "query_position": {
          const { id } = params
          const res = await axios.get(`${BASE_URL}/position`, {
            params: id ? { id } : {},
          })
          const data = res.data
          const positions = Array.isArray(data) ? data : (data?.positions ?? data?.items ?? [])
          if (positions.length === 0) {
            return id ? `标的 ${id} 无持仓` : "当前无持仓"
          }

          const lines = [`**当前持仓**${id ? ` — 标的 ${id}` : ""}（共 ${positions.length} 条）`]
          for (const p of positions.slice(0, 30)) {
            const fields: [string, string][] = []
            if (p.symbol !== undefined || p.id !== undefined) fields.push(["代码", String(p.symbol ?? p.id)])
            if (p.volume !== undefined || p.quantity !== undefined) fields.push(["数量", String(p.volume ?? p.quantity)])
            if (p.available_volume !== undefined) fields.push(["可用", String(p.available_volume)])
            if (p.avg_price !== undefined || p.cost_price !== undefined) {
              fields.push(["成本价", formatNumber((p.avg_price ?? p.cost_price) as number, 2)])
            }
            if (p.last_price !== undefined || p.price !== undefined) {
              fields.push(["现价", formatNumber((p.last_price ?? p.price) as number, 2)])
            }
            if (p.market_value !== undefined) {
              fields.push(["市值", formatNumber(p.market_value as number, 2)])
            }
            if (p.profit !== undefined) {
              fields.push(["浮动盈亏", formatNumber(p.profit as number, 2)])
            }
            const summary = fields.map(([k, v]) => `${k}=${v}`).join(", ")
            lines.push(`  ${summary}`)
          }
          if (positions.length > 30) lines.push(`  ... 共 ${positions.length} 条，仅显示前 30 条`)
          return lines.join("\n")
        }

        case "query_funds": {
          const res = await axios.get(`${BASE_URL}/user/funds`)
          const d = res.data
          if (!d) return "无资金信息"

          const lines = [`**可用资金**`]
          if (d.funds !== undefined) lines.push(`  funds: ${formatNumber(d.funds as number, 2)}`)
          if (d.available !== undefined) lines.push(`  可用: ${formatNumber(d.available as number, 2)}`)
          if (d.frozen !== undefined) lines.push(`  冻结: ${formatNumber(d.frozen as number, 2)}`)
          if (d.total !== undefined) lines.push(`  总资产: ${formatNumber(d.total as number, 2)}`)
          if (d.market_value !== undefined) lines.push(`  持仓市值: ${formatNumber(d.market_value as number, 2)}`)
          // 兼容其他字段
          for (const [k, v] of Object.entries(d as Record<string, any>)) {
            if (['funds', 'available', 'frozen', 'total', 'market_value'].includes(k)) continue
            lines.push(`  ${k}: ${typeof v === 'number' ? formatNumber(v, 2) : JSON.stringify(v).slice(0, 100)}`)
          }
          return lines.join("\n")
        }

        case "query_orders": {
          const { id, type } = params
          const res = await axios.get(`${BASE_URL}/trade/order`, {
            params: {
              ...(id !== undefined ? { id } : {}),
              ...(type !== undefined ? { type } : {}),
            },
          })
          const data = res.data
          const orders = Array.isArray(data) ? data : (data?.orders ?? data?.items ?? [])
          if (orders.length === 0) {
            return id ? `订单 ${id} 不存在或已成交` : "当前无活跃订单"
          }

          const lines = [`**订单状态**${id ? ` — 订单 ${id}` : ""}（共 ${orders.length} 条）`]
          for (const o of orders.slice(0, 30)) {
            const fields: [string, string][] = []
            if (o.symbol !== undefined || o.id !== undefined) fields.push(["代码", String(o.symbol ?? o.id)])
            if (o.direction !== undefined || o.side !== undefined) fields.push(["方向", String(o.direction ?? o.side)])
            if (o.volume !== undefined || o.quantity !== undefined) fields.push(["数量", String(o.volume ?? o.quantity)])
            if (o.price !== undefined) fields.push(["价格", formatNumber(o.price as number, 2)])
            if (o.status !== undefined) fields.push(["状态", String(o.status)])
            if (o.order_id !== undefined || o.sysID !== undefined) fields.push(["单号", String(o.order_id ?? o.sysID)])
            if (o.filled_volume !== undefined) fields.push(["已成交", String(o.filled_volume)])
            const summary = fields.map(([k, v]) => `${k}=${v}`).join(", ")
            lines.push(`  ${summary}`)
          }
          if (orders.length > 30) lines.push(`  ... 共 ${orders.length} 条，仅显示前 30 条`)
          return lines.join("\n")
        }

        default:
          return `未知 action: ${action}`
      }
    } catch (error: any) {
      console.warn("[Order Query Tool] 执行失败:", error)
      const msg = error.response?.data?.error || error.message || "未知错误"
      return `订单查询失败: ${msg}`
    }
  },
  {
    name: "order_query",
    description: [
      '订单与账户查询工具：只读查询持仓、可用资金、订单状态。本工具不下单、不撤单、不修改任何交易状态。',
      '',
      '## WHEN TO USE',
      '- 用户要查“当前持仓/某标的持仓” → query_position',
      '- 用户要查“交易账户可用资金/能买多少” → query_funds',
      '- 用户要查“订单状态/某笔订单成交情况” → query_orders',
      '',
      '## WHEN NOT TO USE',
      '- 用户要“下单/买入/卖出/撤单” → 本工具只读，应提示走交易下单工具或界面',
      '- 用户要查“风控配置/止损线/单日限额” → 用 risk_monitor.query_capital',
      '- 用户要查“策略绩效” → 用 performance.query_strategy_performance',
      '- 用户要“查行情价格” → 用数据管理工具，本工具不返回行情',
      '',
      '## DO NOT USE FOR（跨工具互斥）',
      '- 不要用 query_funds 回答“止损线/风控限额是多少” → 那用 risk_monitor.query_capital',
      '- 不要用本工具回答“某策略表现如何” → performance / risk_monitor',
      '- 下单、撤单、改单请求一律拒绝，明确告知本工具只读',
      '',
      '## ACTION 选择规则',
      '- query_position：id 可选，传则查单标的持仓，不传查全部。返回 代码/数量/可用/成本价/现价/市值/浮动盈亏',
      '- query_funds：无参数。返回 funds/可用/冻结/总资产/持仓市值',
      '- query_orders：id 可选（订单 ID，不是标的代码），type 可选（0=股票 1=期权 2=期货）。不传 type 返回全部类型',
      '',
      '## 参数一参多义提示',
      '- id：在 query_position 中是“标的代码”；在 query_orders 中是“订单 ID”。两者不可互换',
      '- type：仅 query_orders 使用，0/1/2 分别对应股票/期权/期货',
      '',
      '## EXAMPLES',
      '- “我现在持有哪些股票” → query_position',
      '- “茅台持仓多少” → query_position(id="sh.600519")',
      '- “账户里还有多少可用资金” → query_funds',
      '- “今天的股票订单成交了吗” → query_orders(type=0)',
      '- “订单 12345 的状态” → query_orders(id="12345")',
      '- “帮我买入 100 股茅台” → NOT this tool（本工具只读）→ 交易下单工具',
      '- “我的止损线是多少” → NOT this tool → risk_monitor.query_capital',
    ].join('\n'),
    schema: z.object({
      action: z.enum(["query_position", "query_funds", "query_orders"]).describe("操作类型"),
      id: z.union([z.string(), z.number()]).optional().describe("标的代码（query_position）或订单 ID（query_orders）"),
      type: z.number().optional().describe("订单类型: 0-股票 1-期权 2-期货（query_orders 时可选）"),
    }),
  }
)