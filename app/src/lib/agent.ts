/**
 * Agent 模型配置管理 + LangChain LLM Factory
 * 统一多协议 LLM 调用（OpenAI / Anthropic / Custom）
 */

import Anthropic from "@anthropic-ai/sdk"
import { ChatOpenAI } from "@langchain/openai"
import { ChatAnthropic } from "@langchain/anthropic"

// LLM 类型：ChatOpenAI 或 ChatAnthropic
export type LLMInstance = ChatOpenAI | ChatAnthropic

export interface AgentConfig {
  url: string
  protocol: "openai" | "anthropic" | "custom"
  apiKey: string
  model: string
}

const STORAGE_KEY = "quasarx_agent_config"

/**
 * 获取 Agent 配置
 */
export function getAgentConfig(): AgentConfig | null {
  try {
    const stored = localStorage.getItem(STORAGE_KEY)
    if (stored) {
      return JSON.parse(stored)
    }
  } catch (e) {
    console.error("Failed to parse agent config:", e)
  }
  return null
}

/**
 * 保存 Agent 配置
 */
export function saveAgentConfig(config: AgentConfig): void {
  localStorage.setItem(STORAGE_KEY, JSON.stringify(config))
}

/**
 * 删除 Agent 配置
 */
export function removeAgentConfig(): void {
  localStorage.removeItem(STORAGE_KEY)
}

/**
 * LangChain LLM Factory
 * 根据配置创建对应的 LLM 实例
 */
export function createLLM(config: AgentConfig): LLMInstance {
  const commonOptions = {
    model: config.model,
    temperature: 0.7,
    maxTokens: 4096,
  }

  switch (config.protocol) {
    case "openai":
      return new ChatOpenAI({
        ...commonOptions,
        configuration: { baseURL: config.url },
        apiKey: config.apiKey,
      })

    case "anthropic":
      return new ChatAnthropic({
        ...commonOptions,
        apiKey: config.apiKey,
        anthropicApiUrl: config.url,
      })

    default:
      // custom → OpenAI 兼容格式
      return new ChatOpenAI({
        ...commonOptions,
        configuration: { baseURL: config.url },
        apiKey: config.apiKey,
      })
  }
}

/**
 * 测试 Agent 连接
 */
export async function testAgentConnection(config: AgentConfig): Promise<{ success: boolean; message: string }> {
  try {
    if (!config.url) {
      return { success: false, message: "URL 不能为空" }
    }
    if (!config.apiKey) {
      return { success: false, message: "API Key 不能为空" }
    }

    const llm = createLLM(config)
    const response = await llm.invoke([{ role: "user", content: "Hi" } as any])

    const hasContent = typeof response.content === "string"
      ? response.content.length > 0
      : response.content.length > 0

    if (hasContent) {
      return { success: true, message: "连接成功，模型服务可用" }
    }
    return { success: false, message: "连接失败: 未收到有效回复" }
  } catch (e) {
    const errorMessage = e instanceof Error ? e.message : String(e)
    return { success: false, message: `连接失败: ${errorMessage}` }
  }
}
