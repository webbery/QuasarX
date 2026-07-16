/**
 * 数据管理 Tool
 *
 * Agent 可通过此工具：
 * - 查询/下载/删除行情数据（股票/ETF）
 * - 查询/下载/删除财务数据
 * - 查询股票列表、实时信息、基本面
 * - 查询指数行情、期货列表
 * - 导入/导出行情数据
 */

import { tool } from "@langchain/core/tools"
import { z } from "zod"

const BASE_URL = "/v0"

async function getAxios() {
  const mod = await import("axios")
  return mod.default
}

function formatQuoteBar(bar: any): string {
  const dt = typeof bar.datetime === "number"
    ? new Date(bar.datetime * 1000).toLocaleDateString("zh-CN")
    : bar.datetime
  return `  ${dt}  O:${bar.open}  H:${bar.high}  L:${bar.low}  C:${bar.close}  V:${bar.volume}${bar.suspended ? " [停牌]" : ""}`
}

function truncateRows(rows: any[], limit: number, formatter: (r: any) => string): string {
  if (!rows || rows.length === 0) return "  (无数据)"
  const lines = rows.slice(0, limit).map(formatter)
  if (rows.length > limit) {
    lines.push(`  ... 共 ${rows.length} 条，仅显示前 ${limit} 条`)
  }
  return lines.join("\n")
}

export const dataTool = tool(
  async (params) => {
    const { action } = params
    const axios = await getAxios()

    try {
      switch (action) {

        // ========== 行情数据 ==========

        case "list_tables": {
          const res = await axios.get(`${BASE_URL}/quote`)
          const tables = res.data?.tables
          if (!tables || tables.length === 0) return "当前无行情数据表。使用 download_quote 下载数据。"

          const lines = ["**行情数据表**："]
          for (const t of tables) {
            const symbols = t.symbols?.length ?? 0
            const range = t.time_range ? ` (${t.time_range[0]} ~ ${t.time_range[1]})` : ""
            lines.push(`  - **${t.name}**: ${symbols} 个标的${range}`)
          }
          return lines.join("\n")
        }

        case "query_quote": {
          const { table, symbol, start, end, limit } = params
          if (!table || !symbol) return "错误：query_quote 需要 table 和 symbol 参数"

          const res = await axios.get(`${BASE_URL}/quote`, {
            params: { table, symbol, start, end, limit: limit ?? 20 },
          })
          const data = res.data
          if (!data?.data || data.data.length === 0) {
            return `标的 ${symbol} 在 ${table} 中无数据。`
          }

          const header = `**${symbol}** (${table}) — ${data.count ?? data.data.length} 条记录`
          const rows = truncateRows(data.data, 15, formatQuoteBar)
          return `${header}\n${rows}`
        }

        case "download_quote": {
          const { symbols, freq, start, end } = params
          if (!symbols) return "错误：download_quote 需要 symbols 参数（逗号分隔的标的代码）"

          const res = await axios.post(`${BASE_URL}/quote`, {
            symbols,
            freq: freq ?? "daily",
            ...(start ? { start } : {}),
            ...(end ? { end } : {}),
          })
          const groups = res.data?.groups ?? []
          const lines = [`**行情下载已触发** — 标的: ${symbols}，频率: ${freq ?? "daily"}`]
          for (const g of groups) {
            lines.push(`  - ${g.asset_type} → ${g.table}: ${g.symbols?.length ?? 0} 个标的`)
          }
          lines.push("\n数据在后台异步下载，可通过 list_tables 查看进度。")
          return lines.join("\n")
        }

        case "delete_quote": {
          const { table, symbol } = params
          const res = await axios.delete(`${BASE_URL}/quote`, {
            params: { ...(table ? { table } : {}), ...(symbol ? { symbol } : {}) },
          })
          const target = table ? (symbol ? `${table}/${symbol}` : table) : "所有行情数据"
          return `已删除：${target}。${res.data?.message ?? ""}`
        }

        case "import_quote": {
          const { table, symbol, data: rows, adj } = params
          if (!table || !symbol || !rows) return "错误：import_quote 需要 table、symbol、data 参数"

          const res = await axios.post(`${BASE_URL}/quote/data`, {
            action: "import", table, symbol, data: rows, adj: adj ?? "hfq",
          })
          return `导入完成：${table}/${symbol}，${res.data?.imported_rows ?? rows.length} 行。`
        }

        case "export_quote": {
          const { table, symbol, start, end, format: fmt } = params
          if (!table || !symbol) return "错误：export_quote 需要 table 和 symbol 参数"

          const res = await axios.post(`${BASE_URL}/quote/data`, {
            action: "export", table, symbol,
            ...(start ? { start_time: start } : {}),
            ...(end ? { end_time: end } : {}),
            format: fmt ?? "csv",
          })
          if (fmt === "json") {
            const data = res.data?.data
            return `**导出 ${table}/${symbol}**（JSON）\n${JSON.stringify(data, null, 2).slice(0, 3000)}`
          }
          return `**导出 ${table}/${symbol}**（CSV）\n${String(res.data?.data ?? res.data?.message ?? "").slice(0, 3000)}`
        }

        // ========== 财务数据 ==========

        case "list_finance": {
          const res = await axios.get(`${BASE_URL}/finance`)
          const tables = res.data?.tables
          if (!tables || tables.length === 0) return "当前无财务数据。使用 download_finance 下载数据。"

          const lines = ["**财务数据表**："]
          for (const t of tables) {
            lines.push(`  - **${t.name}** (${t.category}): ${t.symbols?.length ?? 0} 个标的`)
          }
          return lines.join("\n")
        }

        case "query_finance": {
          const { category, code, start, end, limit } = params
          if (!category) return "错误：query_finance 需要 category 参数"

          const res = await axios.get(`${BASE_URL}/finance`, {
            params: {
              category,
              ...(code ? { code } : {}),
              ...(start ? { start } : {}),
              ...(end ? { end } : {}),
              limit: limit ?? 20,
            },
          })
          const data = res.data
          if (!data?.data || data.data.length === 0) {
            return `类别 ${category}${code ? `/${code}` : ""} 无数据。`
          }

          const header = `**财务数据** — ${category}${code ? `/${code}` : ""}，${data.count ?? data.data.length} 条`
          const rows = data.data.slice(0, 10).map((row: any) => {
            const entries = Object.entries(row).slice(0, 8)
              .map(([k, v]) => `${k}: ${typeof v === "number" ? v.toFixed(4) : v}`)
              .join(", ")
            return `  ${entries}`
          })
          if (data.data.length > 10) rows.push(`  ... 共 ${data.data.length} 条，仅显示前 10 条`)
          return `${header}\n${rows.join("\n")}`
        }

        case "download_finance": {
          const { code, category, start, end } = params
          if (!code) return "错误：download_finance 需要 code 参数"

          const res = await axios.post(`${BASE_URL}/finance`, {
            code,
            category: category ?? "all",
            ...(start ? { start } : {}),
            ...(end ? { end } : {}),
          })
          return `**财务数据下载已触发** — 标的: ${code}，类别: ${category ?? "all"}。\n${res.data?.status ?? ""}`
        }

        case "delete_finance": {
          const { code, category } = params
          if (!code) return "错误：delete_finance 需要 code 参数"

          const res = await axios.delete(`${BASE_URL}/finance`, {
            params: { code, ...(category ? { category } : {}) },
          })
          return `已删除财务数据：${code}${category ? `/${category}` : ""}。${res.data?.success ? "成功" : ""}`
        }

        // ========== 股票/指数/期货基础信息 ==========

        case "query_stock_info": {
          const res = await axios.get(`${BASE_URL}/stocks/simple`)
          const stocks = res.data?.stocks ?? []
          if (stocks.length === 0) return "无股票数据"

          const lines = [`**股票列表** — 共 ${stocks.length} 只`]
          const show = stocks.slice(0, 30)
          for (const s of show) {
            lines.push(`  ${s.symbol} ${s.name}`)
          }
          if (stocks.length > 30) lines.push(`  ... 共 ${stocks.length} 只，仅显示前 30 只`)
          return lines.join("\n")
        }

        case "query_stock_detail": {
          const { symbol } = params
          if (!symbol) return "错误：query_stock_detail 需要 symbol 参数"

          const res = await axios.get(`${BASE_URL}/stocks/detail`, { params: { id: symbol } })
          const d = res.data
          return `**${symbol} 实时信息**\n  价格: ${d.price ?? "N/A"}  成交量: ${d.volume ?? "N/A"}`
        }

        case "query_stock_verbose": {
          const { symbol } = params
          if (!symbol) return "错误：query_stock_verbose 需要 symbol 参数"

          const res = await axios.get(`${BASE_URL}/stocks/verbose`, { params: { id: symbol } })
          const d = res.data
          const lines = [`**${symbol} 详细信息**`]

          const fields: [string, string][] = [
            ["名称", d.name], ["行业", d.industry], ["地区", d.region],
            ["主营业务", d.main_business], ["总股本", d.total_capital],
            ["流通股本", d.float_capital], ["市盈率", d.pe_ratio],
            ["市净率", d.pb_ratio], ["每股收益", d.eps],
          ]
          for (const [label, val] of fields) {
            if (val !== undefined && val !== null) lines.push(`  ${label}: ${val}`)
          }
          return lines.join("\n")
        }

        case "query_index": {
          const { symbol } = params
          if (!symbol) return "错误：query_index 需要 symbol 参数（如 SH000001,SH000300）"

          const res = await axios.get(`${BASE_URL}/index/quote`, { params: { id: symbol } })
          const d = res.data
          if (Array.isArray(d)) {
            return d.map((item: any) =>
              `**${item.name ?? item.code ?? symbol}**  价格: ${item.price}  涨跌: ${item.change ?? "N/A"}`
            ).join("\n")
          }
          return `**指数 ${symbol}**\n  价格: ${d.price ?? "N/A"}  涨跌: ${d.change ?? "N/A"}`
        }

        case "query_future": {
          const res = await axios.get(`${BASE_URL}/future/simple`)
          const items = res.data ?? []
          if (items.length === 0) return "无期货数据"

          const lines = [`**期货列表** — 共 ${items.length} 个合约`]
          const show = items.slice(0, 30)
          for (const f of show) {
            lines.push(`  ${f.symbol} ${f.name} (${f.exchange ?? ""})`)
          }
          if (items.length > 30) lines.push(`  ... 共 ${items.length} 个，仅显示前 30 个`)
          return lines.join("\n")
        }

        default:
          return `未知 action: ${action}`
      }
    } catch (error: any) {
      console.warn("[Data Tool] 执行失败:", error)
      const msg = error.response?.data?.error || error.message || "未知错误"
      return `数据操作失败: ${msg}`
    }
  },
  {
    name: "data_manager",
    description: [
      "数据管理工具。管理行情和财务数据的查询、下载、删除，以及股票/指数/期货基础信息查询。",
      "action='list_tables' 列出所有行情数据表；",
      "action='query_quote' 查询指定标的K线数据（table+symbol）；",
      "action='download_quote' 触发行情数据下载（symbols=逗号分隔代码，freq=daily/5m/15m/30m/60m）；",
      "action='delete_quote' 删除行情数据；",
      "action='import_quote' 导入CSV行数据到行情表；",
      "action='export_quote' 导出行情数据为CSV/JSON；",
      "action='list_finance' 列出所有财务数据表；",
      "action='query_finance' 查询财务明细（category=profit/operation/growth/balance/cashflow/dupont，code=标的代码）；",
      "action='download_finance' 触发财务数据下载（code=标的代码）；",
      "action='delete_finance' 删除财务数据；",
      "action='query_stock_info' 获取所有股票列表；",
      "action='query_stock_detail' 获取股票实时价格/成交量；",
      "action='query_stock_verbose' 获取股票详细信息（行业/主营/PE/PB等）；",
      "action='query_index' 获取指数行情；",
      "action='query_future' 获取期货合约列表",
    ].join(""),
    schema: z.object({
      action: z.enum([
        "list_tables", "query_quote", "download_quote", "delete_quote",
        "import_quote", "export_quote",
        "list_finance", "query_finance", "download_finance", "delete_finance",
        "query_stock_info", "query_stock_detail", "query_stock_verbose",
        "query_index", "query_future",
      ]).describe("操作类型"),
      table: z.string().optional().describe("行情表名，如 stock_1d, etf_5m, stock_5m"),
      symbol: z.string().optional().describe("标的代码，如 sh.600000（行情）或 SH000001（指数）"),
      symbols: z.string().optional().describe("标的代码列表，逗号分隔（download_quote 时使用）"),
      start: z.string().optional().describe("起始日期 YYYY-MM-DD"),
      end: z.string().optional().describe("结束日期 YYYY-MM-DD"),
      freq: z.string().optional().describe("数据频率: daily/5m/15m/30m/60m（默认 daily）"),
      category: z.string().optional().describe("财务类别: profit/operation/growth/balance/cashflow/dupont/all"),
      code: z.string().optional().describe("财务标的代码，如 600519.SH"),
      format: z.string().optional().describe("导出格式: csv/json（默认 csv）"),
      adj: z.string().optional().describe("复权类型: hfq/none（默认 hfq）"),
      data: z.array(z.string()).optional().describe("CSV 行数据（import_quote 时使用）"),
      limit: z.number().optional().describe("返回条数上限"),
    }),
  }
)
