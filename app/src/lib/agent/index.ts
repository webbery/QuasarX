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
  // 重试策略：LangChain AsyncCaller 层重试 10 次（p-retry 指数退避 + 抖动）。
  // SDK 客户端层由 @langchain/openai 与 @langchain/anthropic 内部强制 maxRetries: 0，
  // 所以不会叠乘 —— 这里的 10 就是真实重试上限（合计 11 次尝试）。
  // 以下情况 AsyncCaller 直接抛出、不重试：
  //   HTTP 400/401/402/403/404/405/406/407/409，以及 insufficient_quota（额度耗尽）。
  // 注意：429 限流（未带 insufficient_quota 码）会走满 10 次退避，最坏约 17 分钟；
  // 用户中止请求可立刻打断（AbortSignal 会 race 掉重试等待）。
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
  renderCapabilityIndex,
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
