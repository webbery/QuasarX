/**
 * 能力缺口登记（显式越界出口）
 *
 * 为什么需要它：
 *   能力地图让 Agent 看得见「哪些事不在范围内」，但看得见不等于会处理。
 *   缺一层显式出口时，模型面对边界只有两条路：编一个答案，或者用相近工具硬凑
 *   （用 javascript 手搓期权定价公式、用 quote 代替 data_manager 拉财务数据）。
 *   report_gap 把「我做不到」变成一个可记录、可统计的结构化动作。
 *
 * 它是常驻工具（不需加载）：任何一轮都可能出现能力边界，不该为此多一次检索。
 */

import { tool } from "@langchain/core/tools"
import type { StructuredToolInterface } from "@langchain/core/tools"
import { z } from "zod"

/** 能力缺口 */
export interface CapabilityGap {
  /** 做不到的子任务 */
  subtask: string
  /** 缺失的能力域 id 或能力名 */
  missing_domain?: string
  /** 为什么做不到 */
  reason: string
  /** 用户可用的替代方案或前置条件 */
  workaround?: string
}

/** 格式化成给用户看的一段话（同时作为事件内容推给 UI） */
export function formatGap(gap: CapabilityGap): string {
  const lines = [`【能力缺口】${gap.subtask}`, `原因：${gap.reason}`]
  if (gap.missing_domain) lines.push(`缺失能力：${gap.missing_domain}`)
  if (gap.workaround) lines.push(`替代方案：${gap.workaround}`)
  return lines.join("\n")
}

/**
 * 创建 report_gap 工具
 *
 * @param onGap 缺口登记回调（由 agent 循环接到事件流，UI 上渲染成「能力缺口」卡片）
 */
export function createGapTool(onGap: (gap: CapabilityGap) => void): StructuredToolInterface {
  return tool(
    async ({ subtask, missing_domain, reason, workaround }) => {
      const gap: CapabilityGap = {
        subtask,
        missing_domain,
        reason,
        workaround,
      }
      onGap(gap)

      const lines = [`已登记能力缺口：${subtask}`, `原因：${reason}`]
      if (missing_domain) lines.push(`缺失能力：${missing_domain}`)
      if (workaround) lines.push(`替代方案：${workaround}`)
      lines.push(
        "在最终回复里必须向用户说明这一步做不到、原因和替代方案；不要用相近的工具硬凑，也不要编造数据。",
      )
      return lines.join("\n")
    },
    {
      name: "report_gap",
      description:
        "登记一个「当前能力集不足以完成」的子任务。当用户要求的某一步超出现有工具与能力域的覆盖范围时调用它 —— " +
        "不要用相近的工具硬凑，也不要编造数据。调用后最终回复必须显式说明这一步做不了、原因和替代方案。",
      schema: z.object({
        subtask: z.string().describe("无法完成的子任务（一句话）"),
        missing_domain: z
          .string()
          .optional()
          .describe("缺失的能力域 id 或能力名（如 market_data）；系统整体没有该能力时省略"),
        reason: z.string().describe("为什么做不到（缺数据 / 缺接口 / 超出职责范围）"),
        workaround: z.string().optional().describe("用户可用的替代方案或需要补齐的前置条件"),
      }),
    },
  )
}
