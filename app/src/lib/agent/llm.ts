/**
 * LLM 工厂：协议分发与重试策略
 *
 * 只依赖 `@langchain/*` 与 `./modelBases`；不 import electron。
 */

import { ChatOpenAI } from "@langchain/openai"
import { ChatAnthropic } from "@langchain/anthropic"
import {
  getActiveModelBase,
  isBaseReady,
  type ModelProtocol,
} from "./modelBases"

export type LLMInstance = ChatOpenAI | ChatAnthropic

export interface AgentConfig {
  url: string
  protocol: ModelProtocol
  apiKey: string
  model: string
}

export function getAgentConfig(): AgentConfig | null {
  const base = getActiveModelBase()
  if (!base || !isBaseReady(base)) return null
  return {
    url: base.url,
    protocol: base.protocol,
    apiKey: base.apiKey,
    model: base.model,
  }
}

/** LLM 默认重试次数（LangChain AsyncCaller 层） */
export const DEFAULT_MAX_RETRIES = 10

export function createLLM(config: AgentConfig, maxRetriesOverride?: number): LLMInstance {
  // 重试由 LangChain AsyncCaller 层执行，maxRetriesOverride 可覆盖次数。
  const maxRetries = maxRetriesOverride ?? DEFAULT_MAX_RETRIES

  const commonOptions = {
    model: config.model,
    temperature: 0.7,
    maxTokens: 4096,
    maxRetries,
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
      return new ChatOpenAI({
        ...commonOptions,
        configuration: { baseURL: config.url },
        apiKey: config.apiKey,
      })
  }
}

export async function testAgentConnection(config: AgentConfig): Promise<{ success: boolean; message: string }> {
  try {
    if (!config.url || !config.apiKey) {
      return { success: false, message: "URL 和 API Key 不能为空" }
    }
    // 连通性探测必须快速失败：重试 10 次会让「测试连接」按钮挂住十几分钟
    const llm = createLLM(config, 0)
    const response = await llm.invoke([{ role: "user", content: "Hi" } as any])
    const hasContent = typeof response.content === "string" ? response.content.length > 0 : response.content.length > 0
    if (hasContent) return { success: true, message: "连接成功，模型服务可用" }
    return { success: false, message: "连接失败: 未收到有效回复" }
  } catch (e) {
    return { success: false, message: `连接失败: ${e instanceof Error ? e.message : String(e)}` }
  }
}
