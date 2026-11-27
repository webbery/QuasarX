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
import { applyAuthHeader, getAuthToken } from "./common/auth"
import { makeArtifact, type ToolArtifactResult } from "@/lib/agent/artifacts"
import { UI_SPEC_VERSION, type UiSpec } from "@/lib/ui/protocol"

const BASE_URL = "/v0"

async function getAxios() {
  const mod = await import("axios")
  const axios = mod.default
  applyAuthHeader(axios)
  return axios
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

/**
 * 删除行情数据（由「待确认动作」调用）
 *
 * 删除不可逆 —— 工具层不直接调它，只产出动作卡，用户点确认后才执行。
 */
export async function deleteQuoteData(params: { table?: string; symbol?: string }): Promise<string> {
  const axios = await getAxios()
  const { table, symbol } = params
  const res = await axios.delete(`${BASE_URL}/quote`, {
    params: { ...(table ? { table } : {}), ...(symbol ? { symbol } : {}) },
  })
  const target = table ? (symbol ? `${table}/${symbol}` : table) : "所有行情数据"
  return `已删除：${target}。${res.data?.message ?? ""}`
}

/**
 * 删除财务数据（由「待确认动作」调用）
 */
export async function deleteFinanceData(params: { code: string; category?: string }): Promise<string> {
  const axios = await getAxios()
  const { code, category } = params
  const res = await axios.delete(`${BASE_URL}/finance`, {
    params: { code, ...(category ? { category } : {}) },
  })
  return `已删除财务数据：${code}${category ? `/${category}` : ""}。${res.data?.success ? "成功" : ""}`
}

export const dataTool = tool(
  async (params) => {
    if (!getAuthToken()) {
      return "错误：未登录或 token 已过期。请先在登录界面完成登录。"
    }
    const { action } = params
    const axios = await getAxios()

    try {
      switch (action) {

        // ========== 行情数据 ==========

        case "list_tables": {
          const res = await axios.get(`${BASE_URL}/quote`)
          // 服务端返回数组：[{ table, symbol_count, symbols: [{ symbol, start_time, end_time, count }] }]
          const tables: any[] = Array.isArray(res.data) ? res.data : (res.data?.tables ?? [])
          if (tables.length === 0) return "当前无行情数据表。使用 download_quote 下载数据。"

          const lines = ["**行情数据表**："]
          for (const t of tables) {
            const name = t.table ?? t.name ?? "未知"
            const symbols: any[] = Array.isArray(t.symbols) ? t.symbols : []
            const count = typeof t.symbol_count === "number" ? t.symbol_count : symbols.length
            const start = symbols[0]?.start_time
            const end = symbols[symbols.length - 1]?.end_time
            const range = start && end ? ` (${start} ~ ${end})` : ""
            lines.push(`  - **${name}**: ${count} 个标的${range}`)
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
          const data = res.data ?? {}
          const freqLabel = freq ?? "daily"
          const rawJobId = data.job_id
          const jobId = typeof rawJobId === "number" ? rawJobId : Number(rawJobId)
          const total = (data.stock_count ?? 0) + (data.etf_count ?? 0)
          const skipped: string[] = Array.isArray(data.skipped_symbols) ? data.skipped_symbols : []

          const lines = [`**行情下载已触发** — 标的: ${symbols}，频率: ${freqLabel}`]
          lines.push(`  - 股票 → ${data.table_stock}: ${data.stock_count ?? 0} 个标的`)
          lines.push(`  - ETF → ${data.table_etf}: ${data.etf_count ?? 0} 个标的`)
          if (skipped.length > 0) {
            // 服务端说这些标的已经在下载了：复用进行中的任务，不再重复下
            lines.push(
              `  - 已在下载中（复用任务 #${jobId}，本次跳过）: ${skipped.join(", ")}`,
            )
          }
          lines.push(
            Number.isFinite(jobId) && jobId > 0
              ? "\n进度在对话里的进度卡上轮询更新（每 3 秒一次）；也可以事后用 list_tables 核对结果。"
              : "\n数据在后台异步下载（服务端未返回 job_id，无法跟踪进度），可用 list_tables 查看。",
          )
          lines.push(
            "\n下载是异步的：数据落库前 query_quote 会查不到，这是正常现象。" +
              "不要因为查不到就重复提交下载，也不用报能力缺口 —— " +
              "告诉用户进度卡在下载，等它跑完再查表。",
          )
          const text = lines.join("\n")

          // 没有 job_id 就没法关联事件 —— 退化成纯文本，不要造一张永远转圈的卡
          if (!Number.isFinite(jobId) || jobId <= 0) return text

          const spec: UiSpec = {
            version: UI_SPEC_VERSION,
            type: "progress",
            title: `行情下载 · ${symbols}`,
            subtitle: `${freqLabel} · ${total} 个标的`,
            params: {},
            data: {
              status: "running",
              label: "已提交，等待服务端开始…",
              // 与 service 的 DataStage 同序；reducer 按下标点亮
              steps: ["下载数据", "数据体检", "入库", "补数据重试"],
              step: 0,
            },
            binding: {
              kind: "poll",
              // 定时轮询服务端的任务状态
              url: `${BASE_URL}/quote?job_id=${jobId}`,
              intervalMs: 3000,
              // 超时没拿到终态就判失败 —— 卡片不能一直转圈
              timeoutMs: 300_000,
            },
          }

          const artifact = makeArtifact("ui", spec.title, spec, {
            subtitle: spec.subtitle,
            source: `quote · #${jobId}`,
            ui: { height: 150, collapsible: false },
          })

          return { text, artifact } as ToolArtifactResult
        }

        case "delete_quote": {
          const { table, symbol } = params
          const target = table ? (symbol ? `${table}/${symbol}` : table) : "所有行情数据"

          // 删除不可逆：产出待确认动作，用户点确认后才真删
          const artifact = makeArtifact(
            "action",
            `删除行情数据 · ${target}`,
            {
              context: `即将删除行情数据：${target}。该操作不可撤销，删除后需要重新下载。`,
              actions: [
                {
                  id: "confirm-delete-quote",
                  kind: "data_manager.delete_quote",
                  label: "确认删除",
                  params: { table, symbol },
                  destructive: true,
                  confirm: {
                    title: `确认删除行情数据「${target}」？`,
                    body: "删除后需要重新下载才能恢复，操作不可撤销。",
                  },
                  pendingLabel: "删除中…",
                  doneLabel: "已删除",
                },
              ],
            },
            { subtitle: target, source: "data_manager · delete_quote" },
          )

          const text = [
            `删除行情数据「${target}」需要用户确认 —— 已生成待确认动作卡。`,
            `在用户点击「确认删除」之前，数据不会被删除。`,
          ].join("\n")
          return { text, artifact } as ToolArtifactResult
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
          const target = `${code}${category ? `/${category}` : ""}`

          // 删除不可逆：产出待确认动作，用户点确认后才真删
          const artifact = makeArtifact(
            "action",
            `删除财务数据 · ${target}`,
            {
              context: `即将删除财务数据：${target}。该操作不可撤销，删除后需要重新下载。`,
              actions: [
                {
                  id: "confirm-delete-finance",
                  kind: "data_manager.delete_finance",
                  label: "确认删除",
                  params: { code, category },
                  destructive: true,
                  confirm: {
                    title: `确认删除财务数据「${target}」？`,
                    body: "删除后需要重新下载才能恢复，操作不可撤销。",
                  },
                  pendingLabel: "删除中…",
                  doneLabel: "已删除",
                },
              ],
            },
            { subtitle: target, source: "data_manager · delete_finance" },
          )

          const text = [
            `删除财务数据「${target}」需要用户确认 —— 已生成待确认动作卡。`,
            `在用户点击「确认删除」之前，数据不会被删除。`,
          ].join("\n")
          return { text, artifact } as ToolArtifactResult
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

        // ========== 期权合约 ==========

        case "query_option_simple": {
          const res = await axios.get(`${BASE_URL}/option/simple`)
          const items = res.data?.items ?? res.data ?? []
          if (items.length === 0) return "无期权合约数据"

          const lines = [`**期权合约列表** — 共 ${items.length} 个`]
          const show = items.slice(0, 30)
          for (const o of show) {
            lines.push(`  ${o.symbol} ${o.name} (${o.exchange ?? ""})`)
          }
          if (items.length > 30) lines.push(`  ... 共 ${items.length} 个，仅显示前 30 个`)
          return lines.join("\n")
        }

        case "query_option_history": {
          const { code, start, end } = params
          if (!code) return "错误：query_option_history 需要 code 参数（期权合约代码）"

          const res = await axios.get(`${BASE_URL}/option/history`, {
            params: {
              id: code,
              ...(start ? { start_date: start } : {}),
              ...(end ? { end_date: end } : {}),
            },
          })
          const d = res.data
          const rows = d?.data ?? d?.bars ?? []
          if (rows.length === 0) return `期权合约 ${code} 无历史行情`

          const header = `**${code} 期权历史行情** — ${rows.length} 条`
          const show = rows.slice(0, 15).map((bar: any) => {
            const dt = typeof bar.datetime === "number"
              ? new Date(bar.datetime * 1000).toLocaleDateString("zh-CN")
              : bar.datetime ?? bar.date ?? ""
            return `  ${dt}  O:${bar.open}  H:${bar.high}  L:${bar.low}  C:${bar.close}  V:${bar.volume ?? "-"}`
          })
          let out = `${header}\n${show.join("\n")}`
          if (rows.length > 15) out += `\n  ... 共 ${rows.length} 条，仅显示前 15 条`
          return out
        }

        case "query_option_data": {
          const { code, start, end } = params
          if (!code) return "错误：query_option_data 需要 code 参数（期权合约代码）"

          const res = await axios.get(`${BASE_URL}/option/data`, {
            params: { symbol: code },
          })
          const d = res.data
          if (!d) return `期权 ${code} 无数据`

          const lines = [`**${code} 期权数据**`]
          if (d.underlying ?? d.underlying_symbol) {
            lines.push(`  标的: ${d.underlying ?? d.underlying_symbol}`)
          }
          if (d.strike !== undefined) lines.push(`  行权价: ${d.strike}`)
          if (d.expiry ?? d.maturity_date) lines.push(`  到期日: ${d.expiry ?? d.maturity_date}`)
          if (d.is_call !== undefined) lines.push(`  类型: ${d.is_call ? "Call" : "Put"}`)
          if (d.implied_volatility !== undefined) lines.push(`  隐含波动率: ${(d.implied_volatility * 100).toFixed(2)}%`)
          if (start || end) lines.push(`  时间范围: ${start ?? "..."} ~ ${end ?? "..."}`)
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
      '数据管理工具：行情/财务数据的查询、下载、删除、导出，以及股票/指数/期货/期权基础信息查询。',
      '本工具只负责“取数与管理数”，不做任何统计检验。',
      '',
      '## WHEN TO USE',
      '- 取数、查数、下载、导入、导出、删除数据',
      '- 列数据表、查标的列表/快照/详情',
      '- 为下游分析工具准备本地数据',
      '',
      '## WHEN NOT TO USE',
      '- 用户要求 ADF/KPSS/协整/Granger 等统计检验 → 用“时间序列计量分析工具”',
      '- 用户只问概念、不涉及具体数据 → 不调用',
      '- 无法确定 action 或关键槽位（table/symbol/category）→ 先向用户澄清，禁止猜测',
      '',
      '## 写操作前置规则（delete_* / import_*）',
      '必须同时满足：(1) 用户明确下达删除/导入指令；(2) 已通过 list_* 确认目标表存在。',
      '缺一即先澄清。执行前回显目标表名与影响范围。',
      '',
      '## ACTION 分组与互斥边界',
      '',
      '### A. 元数据/列表（只读，无前置）',
      '- list_tables：列行情表。query_quote 前 table 未知时先调此 action',
      '- list_finance：列财务表',
      '- query_stock_info：股票列表',
      '- query_future：期货合约列表',
      '- query_option_simple：期权合约列表',
      '',
      '### B. 行情查询（只读，同一请求只选一个）',
      '- query_quote：历史K线，需 table+symbol（频率由 table 名决定，如 stock_1d）。用户说“走势/历史K线”用这个',
      '- query_stock_detail：单只股票实时快照（价格/成交量）。用户说“现在多少钱/实时”用这个，不要用 query_quote',
      '- query_index：指数行情',
      '- query_option_history：期权历史行情（code=合约代码）',
      '- query_option_data：期权合约详情（非行情）',
      '判据：“实时”→detail；“历史K线/走势”→quote；“指数”→index；“期权历史”→option_history；“期权合约信息”→option_data',
      '',
      '### C. 财务查询（只读）',
      '- query_finance：需 category+code。category 不确定时先澄清，不要默认 profit',
      '',
      '### D. 下载/导入（写，异步触发）',
      '- download_quote：触发下载（symbols 逗号分隔多标的 + freq）。下载≠查询，不要用它代替 query_quote',
      '- download_finance：触发财务下载（code）',
      '- import_quote：CSV 导入行情表',
      '',
      '### E. 导出/删除（写）',
      '- export_quote：导出 CSV/JSON',
      '- delete_quote / delete_finance：删除。必须走写操作前置规则',
      '',
      '## 参数一致性',
      '- 单标的用 symbol（query_quote）；多标的用 symbols（download_quote）；不可混用',
      '- freq 仅对 download_quote 有效，取值 daily/5m/15m/30m/60m',
      '- query_quote 的频率由 table 名决定（stock_1d / etf_5m），不要传 freq',
      '',
      '## EXAMPLES',
      '- “查茅台最近K线” → list_tables（table 未知时）→ query_quote(table=..., symbol=sh.600519)',
      '- “茅台现在多少钱” → query_stock_detail(symbol=sh.600519)',
      '- “下载沪深300成分股日线” → download_quote(symbols="...", freq=daily)',
      '- “把行情表导出” → 先确认 table → export_quote',
      '- “检验茅台收益率平稳性” → NOT this tool → 时间序列计量分析工具',
    ].join('\n'),
    schema: z.object({
      action: z.enum([
        "list_tables","query_quote","download_quote","delete_quote","import_quote","export_quote",
        "list_finance","query_finance","download_finance","delete_finance",
        "query_stock_info","query_stock_detail","query_stock_verbose",
        "query_index","query_future","query_option_simple","query_option_history","query_option_data",
      ]).describe("操作类型"),
      table: z.string().optional().describe("行情表名，如 stock_1d / etf_5m；未知时先调 list_tables"),
      symbol: z.string().optional().describe("单标的代码，如 sh.600000（行情）或 SH000001（指数）"),
      symbols: z.string().optional().describe("多标的代码，逗号分隔（仅 download_quote 使用）"),
      start: z.string().optional().describe("起始日期 YYYY-MM-DD"),
      end: z.string().optional().describe("结束日期 YYYY-MM-DD"),
      freq: z.enum(["daily","5m","15m","30m","60m"]).optional()
        .describe("数据频率（仅 download_quote 有效；query_quote 的频率由 table 名决定，不传 freq）"),
      category: z.enum(["all","profit","operation","growth","balance","cashflow","dupont"]).optional()
        .describe("财务类别：query_finance 需明确指定；download_finance 缺省为 all"),
      code: z.string().optional()
        .describe("代码：财务标的代码（如 600519.SH）或期权合约代码（如 10004487.SH）"),
      format: z.enum(["csv","json"]).optional().describe("导出格式（仅 export_quote，默认 csv）"),
      adj: z.enum(["hfq","none"]).optional().describe("复权类型（仅 import_quote，默认 hfq）"),
      data: z.array(z.string()).optional().describe("CSV 行数据（仅 import_quote 使用）"),
      limit: z.number().optional().describe("返回条数上限（query_quote / query_finance）"),
    }),
  }
)
