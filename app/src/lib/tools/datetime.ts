/**
 * 日期时间 Tool
 * 获取当前日期、时间
 */

import { tool } from "@langchain/core/tools"
import { z } from "zod"

export const datetimeTool = tool(
  async ({ type }) => {
    const now = new Date()
    switch (type) {
      case "date":
        return now.toLocaleDateString("zh-CN", {
          year: "numeric",
          month: "long",
          day: "numeric",
          weekday: "long",
        })
      case "time":
        return now.toLocaleTimeString("zh-CN")
      default:
        return now.toLocaleString("zh-CN")
    }
  },
  {
    name: "datetime",
    description: "获取当前日期、时间或日期时间组合。\n\n使用规则：当用户问题包含时间相关词汇（如'最近'、'今天'、'本周'、'本月'、'今年'、'最新'等）或需要时间上下文时，必须先调用此工具获取当前日期，再结合日期使用其他工具（如 quote、knowledge 等）。不要编造日期。",
    schema: z.object({
      type: z.enum(["date", "time", "datetime"]).default("datetime").describe("返回类型：date=日期，time=时间，datetime=日期时间"),
    }),
  }
)
