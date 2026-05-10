/**
 * 账户风险 Tool
 * 获取账户风险指标和市场背景
 */

import { tool } from "@langchain/core/tools"
import { z } from "zod"
import { useChatStore } from "@/stores/chatStore"

export const accountTool = tool(
  async () => {
    const chatStore = useChatStore()
    const marketContext = chatStore.marketContext || "暂无数据"
    const timestamp = new Date().toLocaleString("zh-CN")

    return `【账户风险概览】\n${marketContext}\n数据时间: ${timestamp}`
  },
  {
    name: "account",
    description: "查看账户风险指标、持仓概况和市场背景信息。",
    schema: z.object({}),
  }
)
