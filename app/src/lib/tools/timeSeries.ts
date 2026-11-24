/**
 * 时间序列计量分析 Tool
 *
 * 与 analysis 的分工：analysis 管「算什么」（分解/波动率/变点/PCA/ML），
 * 本工具只管**计量检验**——平稳性、协整、因果这一类假设检验，一个 action 一项。
 *
 * 数据源两种（二选一）：
 * - symbols（+ field 指定 ohlcv 域）→ 后端按标的取数
 * - series → 调用方直接把时间序列给进来
 *
 * adf / kpss 支持 on=residuals：让检验作用在协整回归残差上（EG 两步法的第二步）。
 */

import { tool } from "@langchain/core/tools"
import { z } from "zod"
import { applyAuthHeader, getAuthToken } from "./common/auth"

const BASE_URL = "/v0"

const ACTIONS = [
  "adf", "kpss", "cointegration",
  "johansen_trace", "johansen_max_eigen", "granger",
] as const

async function getAxios() {
  const mod = await import("axios")
  const axios = mod.default
  applyAuthHeader(axios)
  return axios
}

function formatNumber(v: number | undefined | null, digits = 4): string {
  if (v === undefined || v === null || isNaN(v)) return "N/A"
  return Math.abs(v) < 100 ? v.toFixed(digits) : String(v)
}

/** ADF 结果（含 MacKinnon 临界值） */
function formatAdf(adf: any): string[] {
  if (!adf) return ["  ADF: N/A"]
  return [
    `  ADF 统计量: ${formatNumber(adf.statistic)}`,
    `  临界值: 1% ${formatNumber(adf.cv_1pct)} / 5% ${formatNumber(adf.cv_5pct)} / 10% ${formatNumber(adf.cv_10pct)}`,
    `  p 值: ${formatNumber(adf.p_value)}，滞后阶数: ${adf.lags ?? "N/A"}`,
    `  结论: ${adf.is_stationary ? "平稳（拒绝单位根）" : "非平稳（存在单位根）"}`,
  ]
}

/** KPSS 结果（H0 是平稳，与 ADF 相反） */
function formatKpss(kpss: any): string[] {
  if (!kpss) return ["  KPSS: N/A"]
  return [
    `  KPSS 统计量: ${formatNumber(kpss.statistic)}`,
    `  p 值: ${formatNumber(kpss.p_value)}，带宽: ${kpss.lags ?? "N/A"}`,
    `  结论: ${kpss.is_stationary ? "不拒绝 H0（平稳）" : "拒绝 H0（非平稳）"}`,
  ]
}

/** Johansen 逐秩表格 */
function formatJohansenTable(
  label: string,
  stats: number[] | undefined,
  cv95: number[] | undefined,
  cv99: number[] | undefined,
  significant: boolean[] | undefined,
): string[] {
  if (!stats?.length) return []
  const lines = [`\n**${label}**：`, "  秩 | 统计量 | 95% 临界值 | 99% 临界值 | 显著"]
  for (let r = 0; r < stats.length; r++) {
    lines.push(
      `  r=${r} | ${formatNumber(stats[r], 3)} | ${formatNumber(cv95?.[r], 3)} | ${formatNumber(cv99?.[r], 3)} | ${significant?.[r] ? "是" : "否"}`,
    )
  }
  return lines
}

export const timeSeriesTool = tool(
  async (params) => {
    const { action } = params

    if (!getAuthToken()) {
      return "错误：未登录或 token 已过期。请先在登录界面完成登录。"
    }
    if (!params.symbols && !params.series?.length) {
      return "错误：需要数据源。二选一：symbols（如 \"sh.600000,sh.600036\"，可配 field）或 series（二维数组）"
    }

    const axios = await getAxios()

    try {
      const res = await axios.post(`${BASE_URL}/analysis/time_series`, {
        action,
        ...(params.symbols ? { symbols: params.symbols } : {}),
        ...(params.field ? { field: params.field } : {}),
        ...(params.series?.length ? { series: params.series } : {}),
        ...(params.start_date ? { start_date: params.start_date } : {}),
        ...(params.end_date ? { end_date: params.end_date } : {}),
        ...(params.max_lag !== undefined ? { max_lag: params.max_lag } : {}),
        ...(params.on ? { on: params.on } : {}),
      })
      const d = res.data
      if (!d) return "时间序列分析无结果"

      const symbols: string[] = d.symbols ?? []
      const dataRange = symbols.length
        ? `${symbols.join(", ")}（${d.dates?.length ?? 0} 个点）`
        : `${d.dates?.length ?? 0} 个点`

      switch (action) {
        // ========== ADF 单位根检验 ==========
        case "adf": {
          const lines = [`**ADF 单位根检验**：${dataRange}`]
          if (d.pair) {
            lines.push(`  检验对象: 协整回归残差（${d.pair.symbol_x} ~ ${d.pair.symbol_y}）`)
          }
          for (const [key, value] of Object.entries(d.adf ?? {})) {
            lines.push(`\n**${key}**`)
            lines.push(...formatAdf(value))
          }
          return lines.join("\n")
        }

        // ========== KPSS 平稳性检验 ==========
        case "kpss": {
          const lines = [`**KPSS 平稳性检验**：${dataRange}`, "  注意：KPSS 的原假设是「序列平稳」，与 ADF 相反"]
          if (d.pair) {
            lines.push(`  检验对象: 协整回归残差（${d.pair.symbol_x} ~ ${d.pair.symbol_y}）`)
          }
          for (const [key, value] of Object.entries(d.kpss ?? {})) {
            lines.push(`\n**${key}**`)
            lines.push(...formatKpss(value))
          }
          return lines.join("\n")
        }

        // ========== Engle-Granger 协整 ==========
        case "cointegration": {
          const pairs = d.pairwise_eg ?? []
          const lines = [`**Engle-Granger 协整检验**：${dataRange}，${pairs.length} 对`]
          for (const eg of pairs) {
            lines.push(`\n**${eg.symbol_x} ~ ${eg.symbol_y}**`)
            lines.push(`  β=${formatNumber(eg.beta)}，α=${formatNumber(eg.alpha)}，R²=${formatNumber(eg.r_squared)}`)
            lines.push(`  残差 ADF: statistic=${formatNumber(eg.adf?.statistic)}，p=${formatNumber(eg.adf?.p_value)} → ${eg.adf?.is_stationary ? "平稳" : "非平稳"}`)
            lines.push(`  残差 KPSS: statistic=${formatNumber(eg.kpss?.statistic)}，p=${formatNumber(eg.kpss?.p_value)} → ${eg.kpss?.is_stationary ? "平稳" : "非平稳"}`)
            lines.push(`  半衰期: ${formatNumber(eg.half_life, 2)}（AR(1) 反推）；OU θ=${formatNumber(eg.ou?.theta, 4)}，半衰期 ${formatNumber(eg.ou?.half_life, 2)}`)
            lines.push(`  结论: ${eg.is_cointegrated ? "存在协整关系" : "无协整关系"}`)
          }
          return lines.join("\n")
        }

        // ========== Johansen 迹检验 ==========
        case "johansen_trace": {
          const j = d.johansen
          if (!j) return "Johansen 迹检验无结果"
          const lines = [`**Johansen 迹检验**：${dataRange}`]
          lines.push(`  协整秩: ${j.rank}/${j.n_variables}`)
          lines.push(
            ...formatJohansenTable("迹统计量", j.trace_stats, j.trace_cv_95, j.trace_cv_99, j.trace_significant),
          )
          lines.push("\n判读：从 r=0 起逐个检验，第一个不显著的 r 即为协整秩。")
          return lines.join("\n")
        }

        // ========== Johansen 最大特征值检验 ==========
        case "johansen_max_eigen": {
          const j = d.johansen
          if (!j) return "Johansen 最大特征值检验无结果"
          const lines = [`**Johansen 最大特征值检验**：${dataRange}`]
          lines.push(`  协整秩（按迹检验估计）: ${j.rank}/${j.n_variables}`)
          lines.push(
            ...formatJohansenTable("最大特征值统计量", j.max_eigen_stats, j.max_eigen_cv_95, j.max_eigen_cv_99, j.max_eigen_significant),
          )
          lines.push("\n判读：逐秩检验 H0: 协整秩 = r，统计量超过临界值即拒绝。")
          return lines.join("\n")
        }

        // ========== Granger 因果 ==========
        default: {
          const g = d.granger ?? {}
          const lines = [`**Granger 因果检验**：${dataRange}`]

          if (g.pairwise?.length) {
            lines.push("\n**逐对检验**（二元 F 检验）：")
            for (const p of g.pairwise) {
              lines.push(`  ${p.from} → ${p.to ?? "(未指定)"}: F=${formatNumber(p.f_statistic)}，p=${formatNumber(p.p_value)}，最优滞后 ${p.optimal_lag ?? "N/A"} → ${p.is_significant ? "显著" : "不显著"}`)
            }
          }

          if (g.multivariate?.length) {
            lines.push("\n**多元检验**（VAR + Wald）：")
            for (const m of g.multivariate) {
              const cond = m.condition_set?.length ? `（条件集 ${m.condition_set.join(", ")}）` : ""
              lines.push(`  ${m.from} → ${m.to}${cond}: Wald=${formatNumber(m.wald_stat, 3)}，p=${formatNumber(m.p_value)}，最优滞后 ${m.optimal_lag ?? "N/A"} → ${m.is_significant ? "显著" : "不显著"}`)
            }
          }

          if (!g.pairwise?.length && !g.multivariate?.length) {
            lines.push("\n未返回 Granger 检验结果。")
          }
          return lines.join("\n")
        }
      }
    } catch (error: any) {
      console.warn("[TimeSeries Tool] 执行失败:", error)
      const msg = error.response?.data?.error || error.message || "未知错误"
      return `时间序列分析失败: ${msg}`
    }
  },
  {
    name: "time_series",
    description: [
      "时间序列计量分析工具。**一个 action 一项检验**，数值全部来自后端计算。\n",
      "\n**数据源（二选一）**\n",
      "- `symbols`：标的代码，逗号分隔（如 \"sh.600000,sh.600036\"），用 `field` 指定 ohlcv 域\n",
      "- `series`：直接传入时间序列（二维数组，每行一条序列），`symbols` 可用于命名\n",
      "\n**action**\n",
      "- adf ADF 单位根检验（MacKinnon 临界值 + p 值），单序列即可\n",
      "- kpss KPSS 平稳性检验（H0 是平稳，与 ADF 相反），单序列即可\n",
      "- cointegration Engle-Granger 两步法协整（协整回归 + 残差 ADF/KPSS + 半衰期 + OU 拟合），≥2 序列\n",
      "- johansen_trace Johansen 迹检验（逐秩统计量 + 95%/99% 临界值 + 显著性），≥2 序列\n",
      "- johansen_max_eigen Johansen 最大特征值检验，≥2 序列\n",
      "- granger Granger 因果（逐对二元 F 检验 + 多元 VAR/Wald），≥2 序列\n",
      "\n**on=residuals**：adf / kpss 专用，先做协整回归再检验残差（EG 两步法的第二步），要求恰好 2 条序列。\n",
      "\n所有数值必须来自工具返回，禁止编造。",
    ].join(""),
    schema: z.object({
      action: z.enum(ACTIONS).describe("检验类型"),
      symbols: z.string().optional().describe("标的代码，逗号分隔（与 series 二选一；series 模式下用于命名）"),
      field: z.string().optional().describe("ohlcv 域: close/open/high/low/volume/turnover（默认 close）"),
      series: z.array(z.array(z.number())).optional().describe("直接传入的时间序列（二维数组，每行一条）"),
      start_date: z.string().optional().describe("起始日期 YYYY-MM-DD"),
      end_date: z.string().optional().describe("结束日期 YYYY-MM-DD"),
      max_lag: z.number().optional().describe("Granger 最大滞后阶数（默认 10）"),
      on: z.enum(["prices", "residuals"]).optional().describe("adf/kpss 的检验对象：原始序列 prices（默认）或协整残差 residuals"),
    }),
  },
)
