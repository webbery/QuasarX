/**
 * 对话历史管理
 * 自动摘要压缩：超过阈值时调用 LLM 压缩旧历史
 */

import type { LLMInstance } from "./agent"
import { HumanMessage, AIMessage, SystemMessage, BaseMessage } from "@langchain/core/messages"
import type { ChatMessage } from "@/stores/chatStore"

const MAX_MESSAGES = 20          // 保留最近 N 条消息
const COMPRESS_THRESHOLD = 30   // 超过 N 条时触发压缩

export interface CompressResult {
  messages: BaseMessage[]
  summary?: string
}

/**
 * 获取或压缩对话历史
 * - 历史 <= MAX_MESSAGES: 直接返回
 * - 历史 > COMPRESS_THRESHOLD: 压缩旧历史 + 保留最近消息
 */
export async function getOrCompressHistory(
  history: ChatMessage[],
  llm: LLMInstance
): Promise<CompressResult> {
  if (history.length <= MAX_MESSAGES) {
    return { messages: toLangChainMessages(history) }
  }

  // 保留最近 MAX_MESSAGES 条
  const recent = history.slice(-MAX_MESSAGES)
  const toCompress = history.slice(0, -MAX_MESSAGES)

  // 尝试压缩旧历史
  try {
    const summary = await compressConversation(toCompress, llm)
    const summaryMsg = new SystemMessage(`[对话历史摘要] ${summary}`)
    return {
      messages: [summaryMsg, ...toLangChainMessages(recent)],
      summary,
    }
  } catch (e) {
    console.warn("[History] 摘要压缩失败，仅保留最近历史:", e)
    return { messages: toLangChainMessages(recent) }
  }
}

/**
 * 压缩对话历史为摘要
 */
async function compressConversation(
  messages: ChatMessage[],
  llm: LLMInstance
): Promise<string> {
  const text = messages
    .map(m => `${m.role === "user" ? "用户" : "助手"}: ${m.content}`)
    .join("\n\n")

  const prompt = `请将以下对话压缩为一段简洁的摘要（不超过 200 字），保留关键信息、上下文和重要决定：

${text}

摘要：`

  const response = await llm.invoke([new HumanMessage(prompt)])
  return typeof response.content === "string" ? response.content : ""
}

/**
 * ChatMessage 转 LangChain Message
 */
function toLangChainMessages(history: ChatMessage[]): BaseMessage[] {
  return history.map(msg =>
    msg.role === "assistant"
      ? new AIMessage(msg.content)
      : new HumanMessage(msg.content)
  )
}
