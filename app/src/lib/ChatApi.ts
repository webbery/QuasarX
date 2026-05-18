/**
 * Chat API 接口封装（LangChain 单轮 Tool Calling 模式）
 *
 * @deprecated 已迁移到 LangGraph 多 Agent Supervisor 架构。
 * 新代码请使用 `window.ipcRenderer.invoke('langgraph-chat', text)` 调用。
 * 本文件保留作为回退兼容，当 LangGraph 不可用时仍可正常使用。
 *
 * 原说明：使用 LangChain Tool Calling 模式，Agent 自主决定调用哪些 Tool
 */

import { getAgentConfig, createLLM } from "@/lib/agent"
import { getAllTools } from "@/lib/tools"
import { systemPrompt } from "@/lib/prompts/system"
import { getOrCompressHistory } from "@/lib/history"
import type { ChatMessage, ThoughtStep, TokenUsage } from "@/stores/chatStore"
import { HumanMessage, SystemMessage, ToolMessage } from "@langchain/core/messages"
import type { StructuredToolInterface } from "@langchain/core/tools"

export interface AskAIResult {
  answer: string
  thoughts: ThoughtStep[]
  tokenUsage?: TokenUsage  // Token 使用量
}

export interface AskAIProgress {
  thoughts: ThoughtStep[]    // 当前思考步骤列表
  thoughtCount: number       // 当前思考步骤数
  tokenUsage?: TokenUsage    // 累计 token 使用量
  stage: 'thinking' | 'tool' | 'done'  // 当前阶段
  toolName?: string          // 当前调用的工具名称（stage 为 'tool' 时）
}

export interface AskOptions {
  context?: string              // 背景知识上下文（保留兼容）
  history?: ChatMessage[]       // 对话历史
  onChunk?: (text: string) => void
  onDone?: (fullText: string) => void
  onError?: (error: Error) => void
  onProgress?: (progress: AskAIProgress) => void  // 实时进度回调
}

/**
 * 向 AI 助手提问（核心方法）
 *
 * 流程：
 * 1. 创建 LLM（绑定 Tool Calling）
 * 2. 对话历史处理（自动摘要压缩）
 * 3. 构建消息 + 调用 LLM
 * 4. Agent 自主决定调用哪些 Tool
 */

/**
 * @deprecated 使用 LangGraph 多 Agent 替代方案：`ipcRenderer.invoke('langgraph-chat', text)`
 * @param question 用户问题
 * @param options 可选配置
 * @returns AI 回复
 */
export async function askAI(
  question: string,
  options: AskOptions = {}
): Promise<AskAIResult> {
  const { history, onChunk, onDone, onError, onProgress } = options
  const thoughts: ThoughtStep[] = []

  // 累计 token 使用量
  let accumulatedTokenUsage: TokenUsage = {
    promptTokens: 0,
    completionTokens: 0,
    totalTokens: 0
  }

  // 检查 Agent 配置
  const agentConfig = getAgentConfig()
  if (!agentConfig) {
    return {
      answer: "请先在设置中配置 AI 服务。打开 设置 > Agent 大语言模型配置，填写服务 URL、协议和 API Key。",
      thoughts: []
    }
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

    // 步骤 6: Tool Calling 循环执行
    console.log(`[Agent] 发送消息给 LLM (${messages.length} 条), 已绑定 ${tools.length} 个工具`)

    const maxToolCalls = 1000 // 防止无限循环
    let toolCallCount = 0

    while (toolCallCount < maxToolCalls) {
      // 调用 LLM
      const response = await llmWithTools.invoke(messages)

      // 提取消息内容
      const msgContent = response.content
      const hasTextContent = typeof msgContent === "string" && msgContent.trim().length > 0
      const hasTextBlock = Array.isArray(msgContent) && msgContent.some((c: any) => c.type === "text" || c.type === "text_block")

      // 收集 thinking 内容
      if (Array.isArray(msgContent)) {
        for (const c of msgContent) {
          if (c.type === "thinking" && c.thinking) {
            thoughts.push({
              id: c.id || `thought_${Date.now()}_${thoughts.length}`,
              content: c.thinking,
              toolName: c.name,
              timestamp: Date.now()
            })
          }
        }
      }

      // 发送 thinking 内容更新进度
      if (onProgress && thoughts.length > 0) {
        onProgress({
          thoughts: [...thoughts],
          thoughtCount: thoughts.length,
          tokenUsage: { ...accumulatedTokenUsage },
          stage: 'thinking'
        })
      }

      // 收集 tool_calls
      const toolCalls = response.additional_kwargs?.tool_calls
        ?? (Array.isArray(msgContent)
          ? msgContent.filter((c: any) => c.type === "tool_use")
          : [])

      // 如果没有工具调用，说明 LLM 已经完成回复
      if (!toolCalls || toolCalls.length === 0) {
        console.log("[Agent] LLM 完成回复，无更多工具调用")
        break
      }

      toolCallCount++
      console.log(`[Agent] 第 ${toolCallCount} 轮工具调用，共 ${toolCalls.length} 个工具`)

      // 执行每个工具调用
      const toolResults: ToolMessage[] = []
      for (const tc of toolCalls) {
        // 兼容 OpenAI 和 Anthropic 格式
        const toolName = tc.function?.name ?? tc.name ?? "unknown"
        const argsStr = tc.function?.arguments ?? (tc.input ? JSON.stringify(tc.input) : "{}")
        const toolCallId = tc.id ?? `call_${Date.now()}`

        console.log(`[Agent] 执行工具: ${toolName}, 参数: ${argsStr}`)

        // 发送工具调用进度
        if (onProgress) {
          onProgress({
            thoughts: [...thoughts],
            thoughtCount: thoughts.length,
            tokenUsage: { ...accumulatedTokenUsage },
            stage: 'tool',
            toolName
          })
        }

        try {
          // 查找对应的工具
          const tool = tools.find((t: StructuredToolInterface) => t.name === toolName)
          if (!tool) {
            console.error(`[Agent] 未找到工具: ${toolName}`)
            toolResults.push(new ToolMessage({
              content: `错误：未找到工具 "${toolName}"`,
              tool_call_id: toolCallId,
            }))
            thoughts.push({
              id: `thought_tool_${Date.now()}_${thoughts.length}`,
              content: `❌ 工具调用失败：未找到工具 "${toolName}"`,
              toolName,
              timestamp: Date.now()
            })
            continue
          }

          // 解析参数并执行
          const args = typeof argsStr === "string" ? JSON.parse(argsStr) : argsStr
          const result = await tool.invoke(args)
          const resultStr = typeof result === "string" ? result : JSON.stringify(result)

          console.log(`[Agent] 工具 ${toolName} 执行成功，结果长度: ${resultStr.length}`)

          toolResults.push(new ToolMessage({
            content: resultStr,
            tool_call_id: toolCallId,
          }))

          // 添加工具执行结果到思考步骤（显示工具名称和结果预览）
          const preview = resultStr.length > 200 ? resultStr.substring(0, 200) + "..." : resultStr
          thoughts.push({
            id: `thought_tool_${Date.now()}_${thoughts.length}`,
            content: `🔧 调用工具: ${toolName}\n\n${preview}`,
            toolName,
            timestamp: Date.now()
          })

          // 发送工具执行完成进度
          if (onProgress) {
            onProgress({
              thoughts: [...thoughts],
              thoughtCount: thoughts.length,
              tokenUsage: { ...accumulatedTokenUsage },
              stage: 'thinking',
              toolName
            })
          }
        } catch (error) {
          console.error(`[Agent] 工具 ${toolName} 执行失败:`, error)
          toolResults.push(new ToolMessage({
            content: `工具执行错误: ${(error as Error).message}`,
            tool_call_id: toolCallId,
          }))
          thoughts.push({
            id: `thought_tool_${Date.now()}_${thoughts.length}`,
            content: `❌ 工具 "${toolName}" 执行错误: ${(error as Error).message}`,
            toolName,
            timestamp: Date.now()
          })

          // 发送工具执行错误进度
          if (onProgress) {
            onProgress({
              thoughts: [...thoughts],
              thoughtCount: thoughts.length,
              tokenUsage: { ...accumulatedTokenUsage },
              stage: 'thinking',
              toolName
            })
          }
        }
      }

      // 将 LLM 的回复和工具结果添加到消息历史
      messages.push(response)
      messages.push(...toolResults)
    }

    if (toolCallCount >= maxToolCalls) {
      console.warn(`[Agent] 达到最大工具调用次数限制 (${maxToolCalls})，停止执行`)
    }

    // 获取最终回复
    const finalResponse = messages[messages.length - 1]

    // 提取 Token 使用量（从 LLM 响应元数据中）
    let currentTokenUsage: TokenUsage | undefined
    const responseMetadata = (finalResponse as any)?.response_metadata
    const usageMetadata = (finalResponse as any)?.usage_metadata

    if (responseMetadata?.usage) {
      // OpenAI 格式: response_metadata.usage
      const usage = responseMetadata.usage
      currentTokenUsage = {
        promptTokens: usage.prompt_tokens ?? 0,
        completionTokens: usage.completion_tokens ?? 0,
        totalTokens: usage.total_tokens ?? (usage.prompt_tokens ?? 0) + (usage.completion_tokens ?? 0)
      }
    } else if (usageMetadata) {
      // LangChain 统一格式: usage_metadata
      currentTokenUsage = {
        promptTokens: usageMetadata.input_tokens ?? 0,
        completionTokens: usageMetadata.output_tokens ?? 0,
        totalTokens: usageMetadata.total_tokens ?? (usageMetadata.input_tokens ?? 0) + (usageMetadata.output_tokens ?? 0)
      }
    }

    // 累加 token 使用量
    if (currentTokenUsage) {
      accumulatedTokenUsage.promptTokens += currentTokenUsage.promptTokens
      accumulatedTokenUsage.completionTokens += currentTokenUsage.completionTokens
      accumulatedTokenUsage.totalTokens += currentTokenUsage.totalTokens
      console.log(`[Agent] 本轮 Token 使用: prompt=${currentTokenUsage.promptTokens}, completion=${currentTokenUsage.completionTokens}, total=${currentTokenUsage.totalTokens}`)
      console.log(`[Agent] 累计 Token 使用: prompt=${accumulatedTokenUsage.promptTokens}, completion=${accumulatedTokenUsage.completionTokens}, total=${accumulatedTokenUsage.totalTokens}`)
    }

    // 发送最终进度
    if (onProgress) {
      onProgress({
        thoughts: [...thoughts],
        thoughtCount: thoughts.length,
        tokenUsage: { ...accumulatedTokenUsage },
        stage: 'done'
      })
    }

    // 提取回复内容
    // content 可能是字符串或数组（包含 text, thinking, tool_use 等类型）
    let answer = ""
    const finalContent = finalResponse?.content
    
    if (typeof finalContent === "string") {
      answer = finalContent
    } else if (Array.isArray(finalContent)) {
      // 优先提取 text 类型的块
      const textBlocks = (finalContent as Array<{ type?: string; text?: string }>)
        .filter((c: any) => c.type === "text" || c.type === "text_block")
        .map((c: any) => c.text ?? "")
        .join("")
      
      // 如果没有 text 块，尝试其他字段
      answer = textBlocks || (finalContent as Array<any>)
        .map((c: any) => c.text ?? c.content ?? "")
        .join("")
    }

    // 如果最终回复是工具结果，说明 LLM 没有给出文本回复
    if (!answer && finalResponse instanceof ToolMessage) {
      answer = typeof finalResponse.content === "string" 
        ? finalResponse.content 
        : JSON.stringify(finalResponse.content)
    }

    console.log(`[Agent] 最终回复长度: ${answer.length}, 预览: "${answer.substring(0, 100)}${answer.length > 100 ? "..." : ""}"`)

    if (onChunk) {
      onChunk(answer)
    }

    if (onDone) {
      onDone(answer)
    }

    return {
      answer,
      thoughts,
      tokenUsage: accumulatedTokenUsage.totalTokens > 0 ? accumulatedTokenUsage : undefined
    }
  } catch (error) {
    console.error("[Chat] AI 请求失败:", error)

    if (onError) {
      onError(error as Error)
    }

    return {
      answer: `抱歉，AI 服务暂时不可用。错误详情: ${(error as Error).message}`,
      thoughts: [],
      tokenUsage: undefined
    }
  }
}

/**
 * 快捷提问 - 行情分析
 */
export async function askMarketAnalysis(): Promise<AskAIResult> {
  return askAI("请分析当前市场行情，包括：\n1. 主要指数走势\n2. 板块涨跌情况\n3. 市场情绪指标")
}

/**
 * 快捷提问 - 策略建议
 */
export async function askStrategyAdvice(strategyName?: string): Promise<AskAIResult> {
  const question = strategyName
    ? `请为策略"${strategyName}"提供优化建议`
    : "请提供一些策略设计的基本原则和建议"

  return askAI(question)
}

/**
 * 快捷提问 - 风险预警
 */
export async function askRiskWarning(): Promise<AskAIResult> {
  return askAI("请分析我当前的风险状况，并提供风险管理建议")
}

/**
 * 快捷提问 - 持仓诊断
 */
export async function askPositionDiagnosis(): Promise<AskAIResult> {
  return askAI("请分析我当前的持仓结构，并提供调仓建议")
}
