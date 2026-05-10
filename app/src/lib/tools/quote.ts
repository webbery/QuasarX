/**
 * 行情 Tool
 * 获取股票实时行情或历史行情
 */

import { tool } from "@langchain/core/tools"
import { z } from "zod"
import { useQuoteStore } from "@/stores/quoteStore"

export const quoteTool = tool(
  async ({ symbol, date }) => {
    const quoteStore = useQuoteStore()

    if (!symbol) {
      // 默认返回主要指数
      quoteStore.subscribe("sh000001")
      quoteStore.subscribe("sh000300")
      // 等待数据到达
      await new Promise(r => setTimeout(r, 500))
      const sz = quoteStore.getQuote("sh000001")
      const hs = quoteStore.getQuote("sh000300")
      return `【实时行情】\n上证指数: ${sz.lastPrice ?? "-"} (${sz.changePct >= 0 ? "+" : ""}${sz.changePct ?? "-"}%)\n沪深300: ${hs.lastPrice ?? "-"} (${hs.changePct >= 0 ? "+" : ""}${hs.changePct ?? "-"}%)`
    }

    const data = quoteStore.getQuote(symbol)
    return `【行情】${symbol}: ${data.lastPrice ?? "-"} (${data.changePct >= 0 ? "+" : ""}${data.changePct ?? "-"}%)`
  },
  {
    name: "quote",
    description: "获取股票实时行情或指定日期的历史行情。不传 symbol 返回上证指数和沪深300。",
    schema: z.object({
      symbol: z.string().optional().describe("股票代码，如 sh000001（上证指数）、sz399001（深证成指）"),
      date: z.string().optional().describe("日期 YYYY-MM-DD，不传则为实时行情"),
    }),
  }
)
