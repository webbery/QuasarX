/**
 * 数学/统计 Tool
 *
 * Agent 可通过此工具：
 * - 协方差矩阵质量诊断（特征值/条件数/正定性判定）
 * - CUSUM 累积和变点检测（独立入口，复用 analysis 接口）
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

export const mathTool = tool(
  async (params) => {
    if (!getAuthToken()) {
      return "错误：未登录或 token 已过期。请先在登录界面完成登录。"
    }
    const { action } = params
    const axios = await getAxios()

    try {
      switch (action) {

        case "covariance_diagnostics": {
          const { covariance_matrix, labels } = params
          if (!covariance_matrix || !Array.isArray(covariance_matrix)) {
            return "错误：covariance_diagnostics 需要 covariance_matrix 参数（二维数组）"
          }
          const n = covariance_matrix.length
          if (n === 0 || !Array.isArray(covariance_matrix[0])) {
            return "错误：covariance_matrix 必须是二维数组"
          }

          const res = await axios.post(`${BASE_URL}/analysis/covariance_diagnostics`, {
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

          // 解释方差比例（如果返回）
          if (d.eigenvalues && Array.isArray(d.eigenvalues)) {
            const sum = d.eigenvalues.reduce((a: number, b: number) => a + b, 0)
            if (sum > 0) {
              lines.push("\n**方差贡献**：")
              for (let i = 0; i < Math.min(d.eigenvalues.length, n); i++) {
                lines.push(`  λ${i + 1}: ${(d.eigenvalues[i] / sum * 100).toFixed(2)}%`)
              }
            }
          }

          // 诊断建议
          if (d.diagnosis !== undefined) {
            lines.push(`\n诊断: ${d.diagnosis}`)
          } else if (d.is_positive_definite === false) {
            lines.push("\n诊断: 矩阵非正定，建议检查输入协方差矩阵的正确性")
          } else if (d.condition_number > 1e10) {
            lines.push("\n诊断: 条件数过大，矩阵接近奇异，可能导致数值不稳定")
          }

          return lines.join("\n")
        }

        case "cusum": {
          const { symbols, start_date, end_date, threshold, drift, mode } = params
          if (!symbols) return "错误：cusum 需要 symbols 参数"

          const res = await axios.post(`${BASE_URL}/analysis/cusum`, {
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

          if (d.cusum_path && d.dates && Array.isArray(d.cusum_path)) {
            const n = d.cusum_path.length
            const max = Math.max(...d.cusum_path.map((v: number) => Math.abs(v)))
            const peakIdx = d.cusum_path.reduce((maxIdx: number, v: number, i: number, arr: number[]) =>
              Math.abs(v) > Math.abs(arr[maxIdx]) ? i : maxIdx, 0)
            lines.push(`  数据点: ${n}，累积和峰值: ${formatNumber(max)} @ ${d.dates[peakIdx]}`)
          }

          if (d.change_points && Array.isArray(d.change_points) && d.change_points.length > 0) {
            lines.push(`\n**检测到 ${d.change_points.length} 个变点**：`)
            for (const cp of d.change_points.slice(0, 10)) {
              lines.push(`  ${cp.date ?? cp.datetime ?? "?"}: statistic=${formatNumber(cp.statistic ?? cp.cusum_value)}`)
            }
            if (d.change_points.length > 10) lines.push(`  ... 共 ${d.change_points.length} 个`)
          } else {
            lines.push("\n**未检测到显著变点**")
          }

          return lines.join("\n")
        }

        default:
          return `未知 action: ${action}`
      }
    } catch (error: any) {
      console.warn("[Math Tool] 执行失败:", error)
      const msg = error.response?.data?.error || error.message || "未知错误"
      return `数学计算失败: ${msg}`
    }
  },
  {
    name: "math_tool",
    description: [
      "数学与统计分析工具。",
      "action='covariance_diagnostics' 协方差矩阵质量诊断（covariance_matrix 必填，二维数组，labels 可选），返回特征值/条件数/正定性判定；",
      "action='cusum' CUSUM 累积和变点检测（symbols 必填，threshold 默认 4.0，drift 默认 0.5，mode=change_point/momentum/mean_revert 默认 change_point），返回变点与累积和路径",
    ].join(""),
    schema: z.object({
      action: z.enum(["covariance_diagnostics", "cusum"]).describe("操作类型"),
      covariance_matrix: z.array(z.array(z.number())).optional().describe("NxN 协方差矩阵（covariance_diagnostics 必填）"),
      labels: z.array(z.string()).optional().describe("矩阵行列标签（covariance_diagnostics 可选）"),
      symbols: z.string().optional().describe("标的代码，逗号分隔（cusum 必填）"),
      start_date: z.string().optional().describe("起始日期 YYYY-MM-DD（cusum）"),
      end_date: z.string().optional().describe("结束日期 YYYY-MM-DD（cusum）"),
      threshold: z.number().optional().describe("CUSUM 阈值（默认 4.0）"),
      drift: z.number().optional().describe("CUSUM 漂移参数（默认 0.5）"),
      mode: z.string().optional().describe("CUSUM 模式: change_point/momentum/mean_revert（默认 change_point）"),
    }),
  }
)