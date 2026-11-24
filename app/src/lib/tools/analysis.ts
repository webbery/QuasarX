/**
 * 数据分析 Tool（合并 analysis + mutation + math_tool）
 *
 * 合并原则：**算法与数据源解耦**。
 * 原来三个工具是按「数据源 + 算法包」切的（analysis=市场行情、mutation=回测序列、
 * math_tool=用户给矩阵），同一个 CUSUM 因此被实现了两遍。现在统一成一个工具：
 * action 决定算什么，参数决定数据从哪来。
 *
 * 数据源三种：
 * - symbols（+ start_date/end_date/field）→ 后端取行情，走 /v0/analysis/* 与 /v0/ml
 * - target=price/performance/drawdown/dailyReturns → 读最近一次回测结果，本地计算
 * - covariance_matrix（+ labels）→ LLM 直接给矩阵，送后端做诊断
 *
 * 与 backtest 的依赖方向：analysis → backtest（复用 buildChartFromResult），单向无环。
 */

import { tool } from "@langchain/core/tools"
import { z } from "zod"
import { applyAuthHeader, getAuthToken } from "./common/auth"
import { buildChartFromResult } from "./backtest"
import { detectAnomalies, type TimeSeries } from "@/lib/chartData"
import { useHistoryStore } from "@/stores/history"

const BASE_URL = "/v0"

/** 不需要登录（纯本地计算）的 action */
const LOCAL_ACTIONS = new Set(["change_point", "check_date", "list_targets"])

/** 可扫描的回测序列 */
const BACKTEST_TARGETS = ["price", "performance", "drawdown", "dailyReturns"] as const
type BacktestTarget = (typeof BACKTEST_TARGETS)[number]

async function getAxios() {
  const mod = await import("axios")
  const axios = mod.default
  applyAuthHeader(axios)
  return axios
}

function formatNumber(v: number, digits = 4): string {
  if (v === undefined || v === null || isNaN(v)) return "N/A"
  return Math.abs(v) < 100 ? v.toFixed(digits) : String(v)
}

function formatPercent(v: number, digits = 2): string {
  if (v === undefined || v === null || isNaN(v)) return "N/A"
  return `${(v * 100).toFixed(digits)}%`
}

/* ============================================================================
 * 回测序列取数（原 mutation 的能力）
 * ========================================================================== */

/** 把最近一次回测结果转成可扫描的时序 */
async function loadBacktestSeries(target: BacktestTarget): Promise<TimeSeries | null> {
  const historyStore = useHistoryStore()
  const result = await historyStore.getLatestBacktestResult()
  if (!result) return null

  const chartData = buildChartFromResult(result)

  switch (target) {
    case "price":
      return chartData.price
    case "performance":
      return chartData.performance
    case "drawdown":
      return chartData.drawdown
    case "dailyReturns": {
      const rets = chartData.dailyReturns
      if (!rets?.length) return null
      // 日收益率没有自带时间轴，用价格序列起点推（与 buildChartFromResult 的线性分配一致）
      const startTs = chartData.price?.timestamps?.[0] ?? Math.floor(Date.now() / 1000)
      return {
        timestamps: rets.map((_, i) => startTs + i * 86400),
        values: [rets],
        labels: ["日收益率"],
      }
    }
    default:
      return null
  }
}

/** 列出回测结果里实际可用的序列 */
async function listBacktestTargets(): Promise<string[]> {
  const historyStore = useHistoryStore()
  const result = await historyStore.getLatestBacktestResult()
  if (!result) return []

  const chartData = buildChartFromResult(result)
  const available: string[] = []
  if (chartData.price) available.push(`price（价格趋势，${chartData.price.timestamps.length} 个点）`)
  if (chartData.performance) available.push(`performance（累积回报，${chartData.performance.timestamps.length} 天）`)
  if (chartData.drawdown) available.push(`drawdown（回撤曲线，${chartData.drawdown.timestamps.length} 天）`)
  if (chartData.dailyReturns?.length) available.push(`dailyReturns（日收益率，${chartData.dailyReturns.length} 天）`)
  return available
}

/** 解析日期字符串或时间戳 → 毫秒 */
function parseDate(input: string): number | null {
  const ts = Number(input)
  if (!isNaN(ts) && ts > 1e9) return ts < 1e12 ? ts * 1000 : ts

  const d = new Date(input)
  return isNaN(d.getTime()) ? null : d.getTime()
}

/** 格式化一串突变点 */
function formatAnomalies(
  label: string,
  anomalies: Array<{ timestamp: number; value: number; changePct: number; zScore: number; severity: string; context: string }>,
): string[] {
  const lines = [`**${label} 突变检测**（${anomalies.length} 个）`]
  for (const a of anomalies) {
    const icon = a.severity === "high" ? "🔴" : a.severity === "medium" ? "🟡" : "🟢"
    const date = new Date(a.timestamp * 1000).toLocaleDateString("zh-CN")
    lines.push(`${icon} **${date}** — ${a.context}`)
    lines.push(`   值: ${a.value.toFixed(4)} | 变化: ${(a.changePct * 100).toFixed(2)}% | Z-Score: ${a.zScore.toFixed(1)}`)
  }
  return lines
}

/* ============================================================================
 * 工具
 * ========================================================================== */

export const analysisTool = tool(
  async (params) => {
    const { action } = params

    if (!LOCAL_ACTIONS.has(action) && !getAuthToken()) {
      return "错误：未登录或 token 已过期。请先在登录界面完成登录。"
    }

    const axios = LOCAL_ACTIONS.has(action) ? null : await getAxios()

    try {
      switch (action) {
        // ========== 信号分解 ==========

        case "signal_emd": {
          const { symbols, start_date, end_date, field, method, num_imfs, fill_method } = params
          if (!symbols) return "错误：signal_emd 需要 symbols 参数"

          const res = await axios!.get(`${BASE_URL}/analysis/signal`, {
            params: {
              symbols,
              ...(start_date ? { start_date } : {}),
              ...(end_date ? { end_date } : {}),
              field: field ?? "close",
              method: method ?? "emd",
              num_imfs: num_imfs ?? 5,
              ...(fill_method ? { fill_method } : {}),
            },
          })
          const d = res.data
          if (!d) return "信号分析无结果"

          const lines = [`**${method ?? "emd"} 信号分解** — ${symbols}`]
          lines.push(`  字段: ${field ?? "close"}，数据点: ${d.dates?.length ?? 0}`)

          if (d.imf_info && d.imf_info.length > 0) {
            lines.push("\n**IMF 分量**：")
            for (const imf of d.imf_info) {
              lines.push(`  IMF${imf.index}: 平均周期 ${formatNumber(imf.mean_period, 1)} 天，能量占比 ${formatPercent(imf.energy_pct)}`)
            }
          }

          if (d.reconstruction_error !== undefined) {
            lines.push(`\n重建误差: ${formatNumber(d.reconstruction_error, 6)}`)
          }

          return lines.join("\n")
        }

        // ========== 波动率分析 ==========

        case "volatility": {
          const { symbols, start_date, end_date, windows, field, fill_method } = params
          if (!symbols) return "错误：volatility 需要 symbols 参数"

          const symbolList = symbols.split(",").map((s: string) => s.trim())
          const isMulti = symbolList.length > 1

          const res = await axios!.get(`${BASE_URL}/analysis/volatility`, {
            params: {
              symbols,
              ...(start_date ? { start_date } : {}),
              ...(end_date ? { end_date } : {}),
              windows: windows ?? "20,60,120",
              field: field ?? "close",
              ...(fill_method ? { fill_method } : {}),
            },
          })
          const d = res.data
          if (!d) return "波动率分析无结果"

          const lines = [`**波动率分析** — ${symbols}`]

          if (!isMulti && d.single) {
            const sym = symbolList[0]
            const s = d.single[sym]
            if (s) {
              lines.push(`\n**${sym}**：`)
              lines.push(`  年化波动率: ${formatPercent(s.annual_volatility)}`)
              lines.push(`  最大回撤: ${formatPercent(s.max_drawdown)}`)
              lines.push(`  偏度: ${formatNumber(s.skewness)}`)
              lines.push(`  峰度: ${formatNumber(s.kurtosis)}`)
              lines.push(`  VaR(95%): ${formatPercent(s.var_95)}`)
              lines.push(`  CVaR(95%): ${formatPercent(s.cvar_95)}`)

              if (s.acf_decay) {
                lines.push(`  ACF 衰减: ${s.acf_decay.type ?? "N/A"}（半衰期 ${formatNumber(s.acf_decay.half_life, 1)} 天）`)
              }
              if (s.forecast_returns) {
                const fc = s.forecast_returns
                lines.push(`\n**AR 预测**（未来 5 日收益率）：`)
                for (let i = 0; i < Math.min(fc.length, 5); i++) {
                  lines.push(`  T+${i + 1}: ${formatPercent(fc[i])}`)
                }
              }
            }
          }

          if (isMulti && d.multi) {
            const m = d.multi
            lines.push(`\n**多标的分析**（${symbolList.length} 个）：`)

            if (m.correlation_matrix) {
              lines.push("\n相关系数矩阵：")
              const labels = Object.keys(m.correlation_matrix)
              const header = "       " + labels.map((l: string) => l.padStart(10)).join("")
              lines.push(header)
              for (const row of labels) {
                const vals = labels.map((col: string) => {
                  const v = m.correlation_matrix[row]?.[col]
                  return (v !== undefined ? v.toFixed(3) : "N/A").padStart(10)
                }).join("")
                lines.push(`  ${row.padEnd(8)}${vals}`)
              }
            }

            if (m.condition_number !== undefined) {
              lines.push(`\n协方差矩阵条件数: ${formatNumber(m.condition_number, 2)}`)
              lines.push(`正定: ${m.is_positive_definite ? "是" : "否"}`)
            }

            if (m.annual_volatility) {
              lines.push("\n各标的年化波动率：")
              for (const [sym, vol] of Object.entries(m.annual_volatility)) {
                lines.push(`  ${sym}: ${formatPercent(vol as number)}`)
              }
            }
          }

          return lines.join("\n")
        }

        // ========== XGBoost ==========

        case "xgboost_train": {
          const { strategyGraph, label, params: xgbParams } = params
          if (!strategyGraph) return "错误：xgboost_train 需要 strategyGraph 参数（策略图 JSON）"
          if (!label) return "错误：xgboost_train 需要 label 参数（标签配置）"

          const res = await axios!.post(`${BASE_URL}/ml`, {
            action: "train",
            script: JSON.stringify(strategyGraph),
            label,
            ...(xgbParams ? { params: xgbParams } : {}),
          })
          const d = res.data
          if (!d) return "XGBoost 训练无结果"

          const lines = [`**XGBoost 训练完成** — 模型 ID: ${d.model_id}`]
          lines.push(`  训练集: ${d.n_train}，测试集: ${d.n_test}，特征数: ${d.n_features}`)
          lines.push(`  最佳迭代: ${d.best_iteration ?? "N/A"}`)

          if (d.eval_metrics) {
            lines.push("\n**评估指标**：")
            for (const [k, v] of Object.entries(d.eval_metrics)) {
              lines.push(`  ${k}: ${formatNumber(v as number)}`)
            }
          }

          if (d.feature_importance && d.features) {
            const pairs = d.features.map((name: string, i: number) => ({
              name, importance: d.feature_importance[i] ?? 0,
            }))
            pairs.sort((a: any, b: any) => b.importance - a.importance)
            lines.push("\n**特征重要性 Top 10**：")
            for (const p of pairs.slice(0, 10)) {
              lines.push(`  ${p.name}: ${formatNumber(p.importance)}`)
            }
          }

          if (d.learning_curve) {
            const lc = d.learning_curve
            const lastTrain = lc.train?.[lc.train.length - 1]
            const lastTest = lc.test?.[lc.test.length - 1]
            if (lastTrain !== undefined) lines.push(`\n学习曲线终点: train=${formatNumber(lastTrain)}, test=${formatNumber(lastTest ?? NaN)}`)
          }

          return lines.join("\n")
        }

        case "xgboost_shap": {
          const { model_id } = params
          if (!model_id) return "错误：xgboost_shap 需要 model_id 参数"

          const res = await axios!.post(`${BASE_URL}/ml`, {
            action: "shap", model_id,
          })
          const d = res.data
          if (!d) return "SHAP 计算无结果"

          const lines = [`**SHAP 分析** — 模型: ${d.model_id}，样本数: ${d.n_samples}`]

          if (d.features && d.shap) {
            const avgShap = d.features.map((name: string, i: number) => {
              let sum = 0
              for (const row of d.shap) {
                sum += Math.abs(row[i] ?? 0)
              }
              return { name, meanAbs: sum / d.shap.length }
            })
            avgShap.sort((a: any, b: any) => b.meanAbs - a.meanAbs)

            lines.push("\n**SHAP 特征影响 Top 10**（平均 |SHAP|）：")
            for (const s of avgShap.slice(0, 10)) {
              lines.push(`  ${s.name}: ${formatNumber(s.meanAbs)}`)
            }
          }

          if (d.base_value !== undefined) {
            lines.push(`\nBase value: ${formatNumber(d.base_value)}`)
          }

          return lines.join("\n")
        }

        case "xgboost_delete": {
          const { model_id } = params
          if (!model_id) return "错误：xgboost_delete 需要 model_id 参数"

          await axios!.post(`${BASE_URL}/ml`, {
            action: "delete", model_id,
          })
          return `模型 ${model_id} 已释放。`
        }

        // ========== Monte Carlo ==========

        case "monte_carlo": {
          const { symbols, times, steps } = params
          if (!symbols) return "错误：monte_carlo 需要 symbols 参数"

          const res = await axios!.post(`${BASE_URL}/predict/montecarlo`, {
            symbol: symbols,
            times: times ?? 1000,
            type: 2,
            N: steps ?? 20,
            reply: 50,
            start: Math.floor(Date.now() / 1000),
            dt: 86400,
          })
          const d = res.data
          if (!d) return "Monte Carlo 预测无结果"

          const lines = [`**Monte Carlo 预测** — ${symbols}`]
          lines.push(`  模拟 ${times ?? 1000} 次，预测 ${steps ?? 20} 步`)

          if (d.expected) {
            lines.push("\n**期望价格路径**：")
            const exp = d.expected
            for (let i = 0; i < Math.min(exp.length, 10); i++) {
              lines.push(`  T+${i + 1}: ${formatNumber(exp[i], 2)}`)
            }
            if (exp.length > 10) lines.push(`  ... 共 ${exp.length} 步`)
          }

          if (d.paths && d.paths.length > 0) {
            const lastPrices = d.paths.map((p: number[]) => p[p.length - 1])
            lastPrices.sort((a: number, b: number) => a - b)
            const n = lastPrices.length
            lines.push(`\n**终端价格分布**（${n} 条路径）：`)
            lines.push(`  5% 分位: ${formatNumber(lastPrices[Math.floor(n * 0.05)], 2)}`)
            lines.push(`  25% 分位: ${formatNumber(lastPrices[Math.floor(n * 0.25)], 2)}`)
            lines.push(`  50% 分位: ${formatNumber(lastPrices[Math.floor(n * 0.50)], 2)}`)
            lines.push(`  75% 分位: ${formatNumber(lastPrices[Math.floor(n * 0.75)], 2)}`)
            lines.push(`  95% 分位: ${formatNumber(lastPrices[Math.floor(n * 0.95)], 2)}`)
          }

          return lines.join("\n")
        }

        // ========== CUSUM 变点检测（行情序列） ==========

        case "cusum": {
          const { symbols, start_date, end_date, threshold, drift, mode } = params
          if (!symbols) return "错误：cusum 需要 symbols 参数"

          const res = await axios!.post(`${BASE_URL}/analysis/cusum`, {
            symbols,
            ...(start_date ? { start_date } : {}),
            ...(end_date ? { end_date } : {}),
            threshold: threshold ?? 4.0,
            drift: drift ?? 0.5,
            mode: mode ?? "change_point",
          })
          const d = res.data
          if (!d) return "CUSUM 检测无结果"

          const lines = [`**CUSUM 累积和变点检测** — ${symbols}`]
          lines.push(`  模式: ${mode ?? "change_point"}，阈值: ${threshold ?? 4.0}，漂移: ${drift ?? 0.5}`)

          if (d.cusum_path && d.dates && d.dates.length === d.cusum_path.length) {
            const n = d.cusum_path.length
            const max = Math.max(...d.cusum_path.map((v: number) => Math.abs(v)))
            const finalIdx = d.cusum_path.reduce((maxIdx: number, v: number, i: number, arr: number[]) =>
              Math.abs(v) > Math.abs(arr[maxIdx]) ? i : maxIdx, 0)
            lines.push(`  数据点: ${n}，累积和峰值: ${formatNumber(max, 4)} @ ${d.dates[finalIdx]}`)

            lines.push("\n**累积和序列**（前 5 点）：")
            for (let i = 0; i < Math.min(5, n); i++) {
              lines.push(`  ${d.dates[i]}: ${formatNumber(d.cusum_path[i], 4)}`)
            }
            lines.push(`  ...`)
            lines.push(`  ${d.dates[finalIdx]}: ${formatNumber(d.cusum_path[finalIdx], 4)} （峰值）`)
          }

          if (d.change_points && d.change_points.length > 0) {
            lines.push(`\n**检测到 ${d.change_points.length} 个变点**：`)
            for (const cp of d.change_points.slice(0, 10)) {
              lines.push(`  ${cp.date ?? cp.datetime}: statistic=${formatNumber(cp.statistic ?? cp.cusum_value, 4)}, ${cp.significance ? `p=${formatNumber(cp.significance, 4)}` : ""}`)
            }
            if (d.change_points.length > 10) lines.push(`  ... 共 ${d.change_points.length} 个`)
          } else {
            lines.push("\n**未检测到显著变点**")
          }

          return lines.join("\n")
        }

        // ========== 主成分分析 ==========

        case "pca": {
          const { symbols, start_date, end_date, n_components } = params
          if (!symbols) return "错误：pca 需要 symbols 参数（≥2 个，逗号分隔）"

          const symbolList = symbols.split(",").map((s: string) => s.trim()).filter(Boolean)
          if (symbolList.length < 2) return "错误：pca 至少需要 2 个标的"

          const res = await axios!.get(`${BASE_URL}/analysis/pca`, {
            params: {
              symbols,
              ...(start_date ? { start_date } : {}),
              ...(end_date ? { end_date } : {}),
              n_components: n_components ?? 3,
            },
          })
          const d = res.data
          if (!d) return "PCA 分析无结果"

          const lines = [`**主成分分析** — ${symbols}`]
          lines.push(`  标的数: ${symbolList.length}，主成分数: ${n_components ?? 3}`)

          if (d.explained_variance_ratio && d.explained_variance_ratio.length > 0) {
            const ratios = d.explained_variance_ratio
            lines.push("\n**方差解释比例**：")
            let cum = 0
            for (let i = 0; i < ratios.length; i++) {
              cum += ratios[i]
              lines.push(`  PC${i + 1}: ${formatPercent(ratios[i])}（累计 ${formatPercent(cum)}）`)
            }
          }

          if (d.components && d.components.length > 0) {
            lines.push("\n**成分载荷**：")
            for (let i = 0; i < Math.min(d.components.length, 3); i++) {
              const comp = d.components[i]
              const weights = symbolList.map((sym: string, j: number) => `${sym}=${formatNumber(comp[j], 4)}`).join(", ")
              lines.push(`  PC${i + 1}: ${weights}`)
            }
          }

          return lines.join("\n")
        }

        // ========== 协方差矩阵诊断（LLM 直接给矩阵） ==========

        case "covariance_diagnostics": {
          const { covariance_matrix, labels } = params
          if (!covariance_matrix || !Array.isArray(covariance_matrix)) {
            return "错误：covariance_diagnostics 需要 covariance_matrix 参数（二维数组）"
          }
          const n = covariance_matrix.length
          if (n === 0 || !Array.isArray(covariance_matrix[0])) {
            return "错误：covariance_matrix 必须是二维数组"
          }

          const res = await axios!.post(`${BASE_URL}/analysis/covariance_diagnostics`, {
            covariance_matrix,
            labels: labels ?? [],
          })
          const d = res.data
          if (!d) return "协方差诊断无结果"

          const lines = [`**协方差矩阵质量诊断** — ${n}x${n}`]
          if (labels && Array.isArray(labels) && labels.length > 0) {
            lines.push(`  标签: ${labels.join(", ")}`)
          }

          if (d.eigenvalues && Array.isArray(d.eigenvalues)) {
            lines.push("\n**特征值**（降序）：")
            for (let i = 0; i < d.eigenvalues.length; i++) {
              lines.push(`  λ${i + 1}: ${formatNumber(d.eigenvalues[i])}`)
            }
          }

          if (d.condition_number !== undefined) {
            lines.push(`\n条件数 κ: ${formatNumber(d.condition_number, 2)}${d.condition_number > 1e10 ? " ⚠️ 接近奇异" : ""}`)
          }

          if (d.is_positive_definite !== undefined) {
            lines.push(`正定: ${d.is_positive_definite ? "是 ✓" : "否 ✗"}`)
          }

          if (d.min_eigenvalue !== undefined) {
            lines.push(`最小特征值: ${formatNumber(d.min_eigenvalue)}`)
          }

          if (d.max_eigenvalue !== undefined) {
            lines.push(`最大特征值: ${formatNumber(d.max_eigenvalue)}`)
          }

          if (d.eigenvalues && Array.isArray(d.eigenvalues)) {
            const sum = d.eigenvalues.reduce((a: number, b: number) => a + b, 0)
            if (sum > 0) {
              lines.push("\n**方差贡献**：")
              for (let i = 0; i < Math.min(d.eigenvalues.length, n); i++) {
                lines.push(`  λ${i + 1}: ${(d.eigenvalues[i] / sum * 100).toFixed(2)}%`)
              }
            }
          }

          if (d.diagnosis !== undefined) {
            lines.push(`\n诊断: ${d.diagnosis}`)
          } else if (d.is_positive_definite === false) {
            lines.push("\n诊断: 矩阵非正定，建议检查输入协方差矩阵的正确性")
          } else if (d.condition_number > 1e10) {
            lines.push("\n诊断: 条件数过大，矩阵接近奇异，可能导致数值不稳定")
          }

          return lines.join("\n")
        }

        // ========== 回测序列突变扫描 ==========

        case "change_point": {
          const { target, method, windowSize, threshold, minGapDays } = params
          const targetName = (target || "performance") as BacktestTarget

          if (!BACKTEST_TARGETS.includes(targetName)) {
            return `错误：target 只能是 ${BACKTEST_TARGETS.join(" / ")}（收到 "${target}"）`
          }

          const series = await loadBacktestSeries(targetName)
          if (!series) {
            const available = await listBacktestTargets()
            if (available.length === 0) return "当前没有回测结果，无法检测突变。请先在主界面执行一次回测。"
            return `回测结果里没有 ${targetName} 序列。可用序列：\n${available.map((s) => `- ${s}`).join("\n")}`
          }

          const anomalies = detectAnomalies(series, {
            method: (method || "slope") as any,
            windowSize: windowSize ?? 20,
            threshold: threshold ?? 2.5,
            minGapDays: minGapDays ?? 3,
          })

          if (anomalies.length === 0) {
            return `${targetName} 序列未检测到显著突变点（方法 ${method || "slope"}，阈值 ${threshold ?? 2.5}σ），曲线波动在正常范围内。`
          }

          return formatAnomalies(
            `${targetName} 序列（方法 ${method || "slope"}，阈值 ${threshold ?? 2.5}σ）`,
            anomalies,
          ).join("\n")
        }

        // ========== 检查特定日期附近的突变 ==========

        case "check_date": {
          const { target, date, toleranceDays, method, windowSize, threshold, minGapDays } = params
          if (!date) return "错误：check_date 需要 date 参数（YYYY-MM-DD 或时间戳）"

          const targetDate = parseDate(date)
          if (!targetDate) return `无法解析日期: ${date}。请使用 YYYY-MM-DD 格式或 Unix 时间戳`

          const targetName = (target || "performance") as BacktestTarget
          if (!BACKTEST_TARGETS.includes(targetName)) {
            return `错误：target 只能是 ${BACKTEST_TARGETS.join(" / ")}（收到 "${target}"）`
          }

          const series = await loadBacktestSeries(targetName)
          if (!series) {
            const available = await listBacktestTargets()
            if (available.length === 0) return "当前没有回测结果。请先在主界面执行一次回测。"
            return `回测结果里没有 ${targetName} 序列。可用序列：\n${available.map((s) => `- ${s}`).join("\n")}`
          }

          const tolerance = (toleranceDays ?? 5) * 86400000
          const windowStart = targetDate - tolerance
          const windowEnd = targetDate + tolerance

          const anomalies = detectAnomalies(series, {
            method: (method || "slope") as any,
            windowSize: windowSize ?? 20,
            threshold: threshold ?? 2.5,
            // 定点检查放宽最小间隔，避免漏掉邻近的连续突变
            minGapDays: minGapDays ?? 1,
          })

          const nearby = anomalies.filter(
            (a) => a.timestamp * 1000 >= windowStart && a.timestamp * 1000 <= windowEnd,
          )

          if (nearby.length === 0) {
            return `**${targetName}** 在 ${date} 前后 ${toleranceDays ?? 5} 天内未检测到显著突变。`
          }

          const lines = [`**${targetName}** 在 ${date} 前后 ${toleranceDays ?? 5} 天内发现 ${nearby.length} 个突变点：`]
          for (const a of nearby) {
            const d = new Date(a.timestamp * 1000).toLocaleDateString("zh-CN")
            const daysDiff = Math.round((a.timestamp * 1000 - targetDate) / 86400000)
            const when = daysDiff === 0 ? "当天" : daysDiff > 0 ? `后${daysDiff}天` : `前${Math.abs(daysDiff)}天`
            lines.push(`  - ${d}（${when}）: ${a.context}`)
          }
          return lines.join("\n")
        }

        // ========== 列出可扫描的回测序列 ==========

        case "list_targets": {
          const available = await listBacktestTargets()
          if (available.length === 0) {
            return "当前没有回测结果。请先在主界面执行一次回测，之后再列出可扫描序列。"
          }
          return `可扫描的序列：\n${available.map((s) => `- ${s}`).join("\n")}`
        }

        default:
          return `未知 action: ${action}`
      }
    } catch (error: any) {
      console.warn("[Analysis Tool] 执行失败:", error)
      const msg = error.response?.data?.error || error.message || "未知错误"
      return `分析执行失败: ${msg}`
    }
  },
  {
    name: "analysis",
    description: [
      "数据分析工具。**算法与数据源解耦**：action 决定算什么，参数决定数据从哪来。\n",
      "\n**数据源**\n",
      "- `symbols`（可选 start_date/end_date/field，字段默认 close）→ 从后端取行情\n",
      "- `target`=price/performance/drawdown/dailyReturns → 读最近一次回测结果的序列\n",
      "- `covariance_matrix` + `labels` → 你自己把矩阵给进来\n",
      "\n**行情类（用 symbols）**\n",
      "- signal_emd 经验模态分解（method=emd/ceemdan, num_imfs）\n",
      "- volatility 滚动波动率/ACF/AR预测/VaR/CVaR；多标的返回相关矩阵与条件数\n",
      "- cusum CUSUM 累积和变点（threshold 默认 4.0, drift 0.5, mode=change_point/momentum/mean_revert）\n",
      "- pca 主成分分析（n_components 默认 3）\n",
      "- monte_carlo 价格路径模拟（times, steps）\n",
      "- xgboost_train / xgboost_shap / xgboost_delete 机器学习（strategyGraph + label；model_id）\n",
      "\n**回测类（用 target）**\n",
      "- change_point 突变扫描（method=slope/volatility/jump/reversal, windowSize, threshold 默认 2.5σ, minGapDays）\n",
      "- check_date 检查指定日期附近有无突变（date, toleranceDays 默认 5）\n",
      "- list_targets 列出回测结果里可扫描的序列\n",
      "\n**纯计算（用 covariance_matrix）**\n",
      "- covariance_diagnostics 协方差矩阵诊断（特征值/条件数/正定性）\n",
      "\n所有数值必须来自工具返回，禁止编造。",
    ].join(""),
    schema: z.object({
      action: z.enum([
        "signal_emd", "volatility", "cusum", "pca", "monte_carlo",
        "xgboost_train", "xgboost_shap", "xgboost_delete",
        "covariance_diagnostics",
        "change_point", "check_date", "list_targets",
      ]).describe("操作类型"),
      // === 数据源 ===
      symbols: z.string().optional().describe("标的代码，逗号分隔（行情类必填，如 sh.600000,sh.600036）"),
      start_date: z.string().optional().describe("起始日期 YYYY-MM-DD"),
      end_date: z.string().optional().describe("结束日期 YYYY-MM-DD"),
      field: z.string().optional().describe("数据字段: close/open/high/low/volume（默认 close）"),
      fill_method: z.string().optional().describe("缺失值填充: None/Forward/Backward/Linear/Zero"),
      target: z.enum(BACKTEST_TARGETS).optional().describe("回测序列（回测类 action 用）: price/performance/drawdown/dailyReturns，默认 performance"),
      covariance_matrix: z.array(z.array(z.number())).optional().describe("NxN 协方差矩阵（covariance_diagnostics 必填）"),
      labels: z.array(z.string()).optional().describe("矩阵行列标签（covariance_diagnostics 可选）"),
      // === 算法参数 ===
      method: z.string().optional().describe("signal_emd: emd/ceemdan（默认 emd）；change_point: slope/volatility/jump/reversal（默认 slope）"),
      num_imfs: z.number().optional().describe("IMF 数量 1-20（signal_emd 默认 5）"),
      windows: z.string().optional().describe("滚动窗口，逗号分隔（volatility 默认 20,60,120）"),
      n_components: z.number().optional().describe("PCA 主成分数（默认 3）"),
      threshold: z.number().optional().describe("阈值：cusum 默认 4.0；change_point 默认 2.5（Z-Score）"),
      drift: z.number().optional().describe("CUSUM 漂移参数（默认 0.5）"),
      mode: z.string().optional().describe("CUSUM 模式: change_point/momentum/mean_revert（默认 change_point）"),
      times: z.number().optional().describe("Monte Carlo 模拟次数（默认 1000）"),
      steps: z.number().optional().describe("Monte Carlo 预测步数（默认 20）"),
      windowSize: z.number().optional().describe("change_point 局部窗口大小（默认 20）"),
      minGapDays: z.number().optional().describe("change_point 最小间隔天数（默认 3；check_date 默认 1）"),
      date: z.string().optional().describe("目标日期（check_date 必填）: YYYY-MM-DD 或 Unix 时间戳"),
      toleranceDays: z.number().optional().describe("check_date 容差天数（默认 5）"),
      // === XGBoost ===
      strategyGraph: z.any().optional().describe("策略图 JSON（xgboost_train 必填）"),
      label: z.object({
        source: z.string().describe("标签来源"),
        period: z.string().describe("标签周期"),
        type: z.string().describe("标签类型"),
        threshold: z.number().describe("标签阈值"),
      }).optional().describe("标签配置（xgboost_train 必填）"),
      params: z.record(z.string(), z.any()).optional().describe("XGBoost 训练参数"),
      model_id: z.string().optional().describe("模型 ID（xgboost_shap / xgboost_delete 必填）"),
    }),
  },
)
