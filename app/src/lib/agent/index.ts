/**
 * Agent 模块统一导出
 *
 * 包含：
 * - LLM 工厂（createLLM, getAgentConfig 等）
 * - LangGraph Supervisor 架构（supervisor.ts, agents.ts, types.ts）
 * - Checkpoint 持久化（checkpointer.ts）
 * - 内部 Tool 定义（internalTools.ts）
 */

// LLM 工厂（实现见 ./llm；独立成文件是为了让 Node 侧工具能 import 而不牵出 electron 依赖）
export * from "./llm"

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
