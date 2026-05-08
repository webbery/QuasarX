/**
 * Chat API 接口封装（LangChain 重构版）
 *
 * 使用 LangChain Tool Calling 模式，Agent 自主决定调用哪些 Tool
 */

import { getAgentConfig, createLLM } from "@/lib/agent"
import { getAllTools } from "@/lib/tools"
import { systemPrompt } from "@/lib/prompts/system"
import { getOrCompressHistory } from "@/lib/history"
import type { ChatMessage } from "@/stores/chatStore"
import { HumanMessage, SystemMessage } from "@langchain/core/messages"

export interface AskOptions {
  context?: string              // 背景知识上下文（保留兼容）
  history?: ChatMessage[]       // 对话历史
  onChunk?: (text: string) => void
  onDone?: (fullText: string) => void
  onError?: (error: Error) => void
}

/**
 * 向 AI 助手提问（核心方法）
 *
 * 流程：
 * 1. 创建 LLM（绑定 Tool Calling）
 * 2. 对话历史处理（自动摘要压缩）
 * 3. 构建消息 + 调用 LLM
 * 4. Agent 自主决定调用哪些 Tool
 *
 * @param question 用户问题
 * @param options 可选配置
 * @returns AI 回复
 */
export async function askAI(
  question: string,
  options: AskOptions = {}
): Promise<string> {
  const { history, onChunk, onDone, onError } = options

  // 检查 Agent 配置
  const agentConfig = getAgentConfig()
  if (!agentConfig) {
    return "请先在设置中配置 AI 服务。打开 设置 > Agent 大语言模型配置，填写服务 URL、协议和 API Key。"
  }

  try {
    // 步骤 1: 创建 LLM
    const llm = createLLM(agentConfig)

    // 步骤 2: 获取所有 Tool
    const tools = await getAllTools()

    // 步骤 3: 绑定 Tool Calling
    const llmWithTools = (llm as any).bindTools(tools)

    // 步骤 4: 对话历史处理（自动摘要压缩）
    const { messages: compressedHistory } = await getOrCompressHistory(
      history || [],
      llm
    )

    // 步骤 5: 构建消息
    const messages = [
      new SystemMessage(systemPrompt),
      ...compressedHistory,
      new HumanMessage(question),
    ]

    // 步骤 6: 调用 LLM（Tool Calling 自动执行）
    console.log(`[Agent] 发送消息给 LLM (${messages.length} 条), 已绑定 ${tools.length} 个工具`)

    const response = await llmWithTools.invoke(messages, {
      callbacks: [{
        handleLLMEnd(output) {
          // LLM 输出后：检查是否有 tool_calls
          const msg = output.generations[0]?.[0]?.message
          if (!msg) return

          // 收集 tool_calls（可能在 additional_kwargs 或 content 数组中）
          const toolCalls = msg.additional_kwargs?.tool_calls
            ?? (Array.isArray(msg.content)
              ? msg.content.filter((c: any) => c.type === "tool_use")
              : [])

          if (toolCalls.length > 0) {
            console.log("[Agent] LLM 决定调用工具:")
            for (const tc of toolCalls) {
              // 兼容 OpenAI 格式 (function.arguments) 和 Anthropic 格式 (input)
              const args = tc.function?.arguments
                ?? (tc.input ? JSON.stringify(tc.input) : "{}")
              const name = tc.function?.name ?? tc.name ?? "unknown"
              console.log(`  ├── Tool: ${name}`)
              console.log(`  └── Args: ${args}`)
            }
          }

          // 提取文本内容（content 可能是数组）
          const textBlock = Array.isArray(msg.content)
            ? msg.content.find((c: any) => c.type === "text")?.text ?? ""
            : (typeof msg.content === "string" ? msg.content : "")

          if (textBlock && toolCalls.length === 0) {
            const preview = textBlock.substring(0, 100)
            console.log(`[Agent] LLM 直接回复（未调用工具）: "${preview}${textBlock.length > 100 ? "..." : ""}"`)
          } else if (textBlock && toolCalls.length > 0) {
            const preview = textBlock.substring(0, 100)
            console.log(`[Agent] LLM 附带文本: "${preview}${textBlock.length > 100 ? "..." : ""}"`)
          }
        },
        handleToolEnd(output) {
          // Tool 执行完成后
          const preview = typeof output === "string" ? output : JSON.stringify(output)
          console.log(`[Tool Result] ${preview.substring(0, 200)}${preview.length > 200 ? "..." : ""}`)
        },
      }]
    })

    // 提取回复内容
    const answer = typeof response.content === "string"
      ? response.content
      : (response.content as Array<{ type?: string; text?: string }>).map((c: any) => c.text ?? "").join("")

    if (onChunk) {
      onChunk(answer)
    }

    if (onDone) {
      onDone(answer)
    }

    return answer
  } catch (error) {
    console.error("[Chat] AI 请求失败:", error)

    if (onError) {
      onError(error as Error)
    }

    return `抱歉，AI 服务暂时不可用。错误详情: ${(error as Error).message}`
  }
}

/**
 * 向 AI 助手提问（兼容旧版接口，支持 LLM 回调用于摘要压缩）
 *
 * @deprecated 使用 askAI() 即可，摘要压缩已自动处理
 */
export async function askAIWithSummary(
  question: string,
  _llmCallback: (prompt: string) => Promise<string>,
  options: AskOptions = {}
): Promise<string> {
  // 直接委托给 askAI，摘要压缩在 getOrCompressHistory 中自动处理
  return askAI(question, options)
}

/**
 * 快捷提问 - 行情分析
 */
export async function askMarketAnalysis(): Promise<string> {
  return askAI("请分析当前市场行情，包括：\n1. 主要指数走势\n2. 板块涨跌情况\n3. 市场情绪指标")
}

/**
 * 快捷提问 - 策略建议
 */
export async function askStrategyAdvice(strategyName?: string): Promise<string> {
  const question = strategyName
    ? `请为策略"${strategyName}"提供优化建议`
    : "请提供一些策略设计的基本原则和建议"

  return askAI(question)
}

/**
 * 快捷提问 - 风险预警
 */
export async function askRiskWarning(): Promise<string> {
  return askAI("请分析我当前的风险状况，并提供风险管理建议")
}

/**
 * 快捷提问 - 持仓诊断
 */
export async function askPositionDiagnosis(): Promise<string> {
  return askAI("请分析我当前的持仓结构，并提供调仓建议")
}
