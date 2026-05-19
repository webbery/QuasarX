/**
 * Agent 模块统一导出
 *
 * 包含：
 * - LLM 工厂（createLLM, getAgentConfig 等）
 * - LangGraph Supervisor 架构（supervisor.ts, agents.ts, types.ts）
 * - Checkpoint 持久化（checkpointer.ts）
 * - 内部 Tool 定义（internalTools.ts）
 */

// LLM 工厂（从 agent.ts 合并）
import Anthropic from "@anthropic-ai/sdk"
import { ChatOpenAI } from "@langchain/openai"
import { ChatAnthropic } from "@langchain/anthropic"

export type LLMInstance = ChatOpenAI | ChatAnthropic

export interface AgentConfig {
  url: string
  protocol: "openai" | "anthropic" | "custom"
  apiKey: string
  model: string
}

const STORAGE_KEY = "quasarx_agent_config"

export function getAgentConfig(): AgentConfig | null {
  try {
    const stored = localStorage.getItem(STORAGE_KEY)
    if (stored) return JSON.parse(stored)
  } catch (e) {
    console.error("Failed to parse agent config:", e)
  }
  return null
}

export function saveAgentConfig(config: AgentConfig): void {
  localStorage.setItem(STORAGE_KEY, JSON.stringify(config))
}

export function removeAgentConfig(): void {
  localStorage.removeItem(STORAGE_KEY)
}

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
    const llm = createLLM(config)
    const response = await llm.invoke([{ role: "user", content: "Hi" } as any])
    const hasContent = typeof response.content === "string" ? response.content.length > 0 : response.content.length > 0
    if (hasContent) return { success: true, message: "连接成功，模型服务可用" }
    return { success: false, message: "连接失败: 未收到有效回复" }
  } catch (e) {
    return { success: false, message: `连接失败: ${e instanceof Error ? e.message : String(e)}` }
  }
}

// Supervisor
export { runSupervisorGraph, buildSupervisorGraph } from "./supervisor"

// Checkpointer
export { IndexedDBSaver } from "./checkpointer"

// Types
export type { AgentEvent, GraphStateType, AgentType, RouterDecision } from "./types"
export { AGENT_TOOL_CONFIG, AGENT_SYSTEM_PROMPTS } from "./types"

// Internal Tools
export { internalTools, INTERNAL_TOOL_AGENT_MAP } from "./internalTools"

// Agent Node Creation
export { getToolsForAgent, createAgentNode } from "./agents"
