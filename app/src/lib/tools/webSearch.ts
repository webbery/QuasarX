/**
 * 网络搜索/爬虫 Tool
 * 从网络获取信息，支持两种方式：
 * 1. Tavily 搜索：使用 Tavily API 搜索网络信息（需配置 API Key）
 * 2. 直接抓取：直接抓取指定 URL 的网页内容
 * 
 * 使用场景：
 * - 查询实时新闻、公告、政策
 * - 获取网络上公开的信息
 * - 抓取指定网页的内容
 */

import { tool } from "@langchain/core/tools"
import { z } from "zod"

/**
 * 获取 Tavily API Key
 */
function getTavilyApiKey(): string {
  // 从 localStorage 读取（设置页面保存的位置）
  if (typeof window !== "undefined") {
    return localStorage.getItem("tavily_api_key") || ""
  }
  return ""
}

/**
 * 使用 Tavily API 搜索
 * 返回结构化的搜索结果
 */
async function searchWithTavily(query: string, maxResults: number = 3): Promise<string> {
  const apiKey = getTavilyApiKey()
  
  if (!apiKey) {
    return `【Tavily 搜索未配置】\n\n请先在设置中配置 Tavily API Key：\n1. 打开设置页面\n2. 找到"Tavily 网络搜索配置"\n3. 输入 API Key（在 https://tavily.com 注册获取）\n4. 点击保存\n\n您的查询："${query}"`
  }

  try {
    console.log(`[WebSearch] 使用 Tavily 搜索: ${query}`)
    
    const response = await fetch("https://api.tavily.com/search", {
      method: "POST",
      headers: {
        "Content-Type": "application/json",
      },
      body: JSON.stringify({
        api_key: apiKey,
        query,
        max_results: maxResults,
        search_depth: "basic", // 或 "advanced"
        include_answer: true,  // 包含 AI 生成的答案
      }),
      signal: AbortSignal.timeout(15000), // 15 秒超时
    })

    if (!response.ok) {
      const error = await response.json()
      throw new Error(error.detail?.error || `HTTP ${response.status}`)
    }

    const data = await response.json()

    // 构建格式化的搜索结果
    // 包含概要整理和编号引用链接 [1][2][3]
    let result = `【网络搜索】查询："${query}"\n\n`

    // 如果有 AI 摘要，优先显示
    if (data.answer) {
      result += `${data.answer}\n\n`
    }

    // 显示具体搜索结果，使用 [1][2][3] 编号格式
    if (data.results && data.results.length > 0) {
      result += `**参考来源：**\n`

      for (let i = 0; i < data.results.length; i++) {
        const r = data.results[i]
        const num = i + 1
        const title = r.title || "无标题"
        const url = r.url
        
        // 格式：[1] 标题 - URL
        result += `[${num}] ${title} - ${url}\n`
      }
      
      result += `\n> 提示：以上内容基于网络搜索结果整理，请核实重要信息。`
    } else {
      result += "未找到相关搜索结果。"
    }

    return result
  } catch (error) {
    console.error("[WebSearch] Tavily 搜索失败:", error)
    return `【Tavily 搜索失败】\n\n错误信息: ${(error as Error).message}\n\n建议：\n1. 检查 API Key 是否正确\n2. 检查网络连接\n3. 确认 Tavily 账户额度是否充足\n\n您的查询："${query}"`
  }
}

/**
 * 抓取单个网页的文本内容
 * 移除 HTML 标签，返回纯文本
 */
async function fetchPageContent(url: string): Promise<string> {
  try {
    console.log(`[WebSearch] 抓取: ${url}`)
    
    const response = await fetch(url, {
      method: "GET",
      headers: {
        "Accept": "text/html,application/xhtml+xml",
        "User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36",
        "Accept-Language": "zh-CN,zh;q=0.9",
      },
      signal: AbortSignal.timeout(10000), // 10 秒超时
    })

    if (!response.ok) {
      throw new Error(`HTTP ${response.status}`)
    }

    const contentType = response.headers.get("content-type") || ""
    if (!contentType.includes("text/html") && !contentType.includes("text/plain")) {
      throw new Error(`不支持的内容类型: ${contentType}`)
    }

    const html = await response.text()
    
    // 提取文本内容
    const text = html
      .replace(/<script[^>]*>[\s\S]*?<\/script>/gi, "")  // 移除脚本
      .replace(/<style[^>]*>[\s\S]*?<\/style>/gi, "")    // 移除样式
      .replace(/<nav[^>]*>[\s\S]*?<\/nav>/gi, "")        // 移除导航
      .replace(/<header[^>]*>[\s\S]*?<\/header>/gi, "")  // 移除头部
      .replace(/<footer[^>]*>[\s\S]*?<\/footer>/gi, "")  // 移除底部
      .replace(/<[^>]+>/g, " ")                           // 移除所有标签
      .replace(/\s+/g, " ")                               // 合并空白
      .replace(/&nbsp;/g, " ")                            // 处理 HTML 实体
      .trim()

    // 返回前 3000 字符作为内容
    return text.substring(0, 3000) + (text.length > 3000 ? "..." : "")
  } catch (error) {
    console.error(`[WebSearch] 抓取 ${url} 失败:`, error)
    throw error
  }
}

export const webSearchTool = tool(
  async ({ url, query, maxResults }) => {
    // 模式 1: 直接抓取指定 URL
    if (url) {
      try {
        const content = await fetchPageContent(url)
        return `【网页内容】${url}\n\n${content}`
      } catch (error) {
        return `抓取失败: ${(error as Error).message}\n\n建议：1) 检查 URL 是否正确；2) 提供其他 URL；3) 使用 Tavily 搜索查询。`
      }
    }

    // 模式 2: Tavily 搜索（优先使用）
    if (query) {
      return await searchWithTavily(query, maxResults)
    }

    return "请提供 url（要抓取的网页地址）或 query（搜索关键词）参数。"
  },
  {
    name: "web_search",
    description: "从网络获取信息。优先使用 Tavily API 搜索（需配置 API Key），也支持直接抓取指定 URL。用法：1) query 参数：搜索关键词；2) url 参数：要抓取的网页地址。",
    schema: z.object({
      url: z.string().optional().describe("要抓取的网页 URL（完整地址）"),
      query: z.string().optional().describe("搜索关键词或查询短语（使用 Tavily API 搜索）"),
      maxResults: z.number().default(3).describe("返回的最大搜索结果数（默认 3）"),
    }),
  }
)
