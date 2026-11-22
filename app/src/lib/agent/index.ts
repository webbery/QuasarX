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

export function createLLM(config: AgentConfig): LLMInstance {
  // 不自动重试：额度耗尽（429）重试无意义，只会让用户干等。
  // 三层都要关：LangChain AsyncCaller（默认 6 次重试）→ SDK 客户端（默认 2 次）→ 上层图节点。
  const maxRetries = 0

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
        configuration: { baseURL: config.url, maxRetries },
        apiKey: config.apiKey,
      })
    case "anthropic":
      return new ChatAnthropic({
        ...commonOptions,
        apiKey: config.apiKey,
        anthropicApiUrl: config.url,
        clientOptions: { maxRetries },
      })
    default:
      return new ChatOpenAI({
        ...commonOptions,
        configuration: { baseURL: config.url, maxRetries },
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
export { getResidentToolsForAgent, createAgentNode } from "./agents"

// 工具目录与统一检索（渐进式披露）
export {
  ToolCatalog,
  buildToolCatalog,
  getToolCatalog,
  tokenizeQuery,
  type ToolCatalogEntry,
  type ToolCatalogMatch,
} from "./toolCatalog"
export { createSearchTool, type SearchToolDeps } from "./searchTool"

// 能力地图（规划阶段的认知底物）与越界出口
export {
  CAPABILITY_DOMAINS,
  AGENT_DOMAINS,
  getDomain,
  domainsForAgent,
  renderCapabilityMap,
  checkCapabilityMap,
  verifyCapabilityMap,
  type CapabilityDomain,
} from "./capabilityMap"
export { createGapTool, formatGap, type CapabilityGap } from "./gapTool"

// 工作流痕迹导出（调试用）
export {
  formatEventsMarkdown,
  buildTraceJson,
  traceFilename,
  type TraceMessage,
} from "./traceExport"
