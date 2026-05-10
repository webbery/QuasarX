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
    description: "获取当前日期、时间或日期时间组合。",
    schema: z.object({
      type: z.enum(["date", "time", "datetime"]).default("datetime").describe("返回类型：date=日期，time=时间，datetime=日期时间"),
    }),
  }
)
