/**
 * 数据分析 Tool
 *
 * Agent 可通过此工具：
 * - EMD/CEEMDAN 信号分解分析
 * - 波动率/自相关/协方差分析
 * - XGBoost 训练/SHAP 分析
 * - Monte Carlo 价格预测
 */

import { tool } from "@langchain/core/tools"
import { z } from "zod"

const BASE_URL = "/v0"

async function getAxios() {
  const mod = await import("axios")
  return mod.default
}

function formatNumber(v: number, digits = 4): string {
  if (v === undefined || v === null || isNaN(v)) return "N/A"
  return Math.abs(v) < 100 ? v.toFixed(digits) : String(v)
}

function formatPercent(v: number, digits = 2): string {
  if (v === undefined || v === null || isNaN(v)) return "N/A"
  return `${(v * 100).toFixed(digits)}%`
}

export const analysisTool = tool(
  async (params) => {
    const { action } = params
    const axios = await getAxios()

    try {
      switch (action) {

        // ========== 信号分析 ==========

        case "signal_emd": {
          const { symbols, start_date, end_date, field, method, num_imfs, fill_method } = params
          if (!symbols) return "错误：signal_emd 需要 symbols 参数"

          const res = await axios.get(`${BASE_URL}/analysis/signal`, {
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

          const res = await axios.get(`${BASE_URL}/analysis/volatility`, {
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

          const res = await axios.post(`${BASE_URL}/xgboost`, {
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

          const res = await axios.post(`${BASE_URL}/xgboost`, {
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

          await axios.post(`${BASE_URL}/xgboost`, {
            action: "delete", model_id,
          })
          return `模型 ${model_id} 已释放。`
        }

        // ========== Monte Carlo ==========

        case "monte_carlo": {
          const { symbols, times, steps } = params
          if (!symbols) return "错误：monte_carlo 需要 symbols 参数"

          const res = await axios.post(`${BASE_URL}/predict/montecarlo`, {
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
      "数据分析工具。对标的进行信号分解、波动率分析、机器学习训练和价格预测。",
      "action='signal_emd' EMD/CEEMDAN 信号分解（symbols=标的代码，method=emd/ceemdan，num_imfs=IMF数量）；",
      "action='volatility' 波动率与自相关分析，单标的返回滚动波动率/VaR/ACF/AR预测，多标的返回协方差/相关矩阵（symbols=逗号分隔）；",
      "action='xgboost_train' XGBoost 训练（strategyGraph=策略图JSON，label={source,period,type,threshold}）；",
      "action='xgboost_shap' 计算 SHAP 值（model_id=模型ID）；",
      "action='xgboost_delete' 释放模型（model_id=模型ID）；",
      "action='monte_carlo' Monte Carlo 价格预测（symbols=标的，times=模拟次数，steps=预测步数）",
    ].join(""),
    schema: z.object({
      action: z.enum([
        "signal_emd", "volatility",
        "xgboost_train", "xgboost_shap", "xgboost_delete",
        "monte_carlo",
      ]).describe("操作类型"),
      symbols: z.string().optional().describe("标的代码，逗号分隔（如 sh.600000 或 sh.600000,sh.600036）"),
      start_date: z.string().optional().describe("起始日期 YYYY-MM-DD"),
      end_date: z.string().optional().describe("结束日期 YYYY-MM-DD"),
      field: z.string().optional().describe("数据字段: close/open/high/low/volume（默认 close）"),
      method: z.string().optional().describe("分解方法: emd/ceemdan（默认 emd）"),
      num_imfs: z.number().optional().describe("IMF 数量 (1-20，默认 5)"),
      windows: z.string().optional().describe("滚动窗口，逗号分隔（如 20,60,120）"),
      fill_method: z.string().optional().describe("缺失值填充: None/Forward/Backward/Linear/Zero"),
      strategyGraph: z.any().optional().describe("策略图 JSON（xgboost_train 需要）"),
      label: z.object({
        source: z.string().describe("标签来源"),
        period: z.string().describe("标签周期"),
        type: z.string().describe("标签类型"),
        threshold: z.number().describe("标签阈值"),
      }).optional().describe("标签配置（xgboost_train 需要）"),
      params: z.record(z.string(), z.any()).optional().describe("XGBoost 训练参数"),
      model_id: z.string().optional().describe("模型 ID（xgboost_shap/xgboost_delete 需要）"),
      times: z.number().optional().describe("Monte Carlo 模拟次数（默认 1000）"),
      steps: z.number().optional().describe("Monte Carlo 预测步数（默认 20）"),
    }),
  }
)
