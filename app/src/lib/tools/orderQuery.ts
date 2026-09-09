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
      "订单与账户查询工具。",
      "action='query_position' 查询当前持仓（id 可选，指定时返回单标的明细）；",
      "action='query_funds' 查询可用资金；",
      "action='query_orders' 查询订单状态（id 可选指定订单，type=0 股票/1 期权/2 期货）",
    ].join(""),
    schema: z.object({
      action: z.enum(["query_position", "query_funds", "query_orders"]).describe("操作类型"),
      id: z.union([z.string(), z.number()]).optional().describe("标的代码（query_position）或订单 ID（query_orders）"),
      type: z.number().optional().describe("订单类型: 0-股票 1-期权 2-期货（query_orders 时可选）"),
    }),
  }
)