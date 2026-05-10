/**
 * Tool 元数据类型定义
 * 用于 Tool 分类、自动发现和插件系统
 */

import type { StructuredToolInterface } from "@langchain/core/tools"

export type ToolCategory = "data" | "system" | "knowledge" | "trade" | "analysis"

export interface ToolMeta {
  name: string          // Tool 名称（唯一）
  category: ToolCategory
  description: string   // 简短描述，用于 Agent 理解 Tool 用途
  enabled?: boolean     // 默认是否启用
}

export interface ToolModule {
  meta: ToolMeta
  create: () => StructuredToolInterface | StructuredToolInterface[]
}
