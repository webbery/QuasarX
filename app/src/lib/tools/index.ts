/**
 * Tool 统一注册中心
 *
 * 两种来源：
 * 1. 内置 Tool：静态导入
 * 2. 插件 Tool：从 plugins/ 目录自动加载
 *
 * 扩展方式：
 * - 内置：在 builtinTools 数组中添加
 * - 插件：在 plugins/ 目录下创建 .ts 文件，默认导出 StructuredToolInterface
 */

import type { StructuredToolInterface } from "@langchain/core/tools"

// === 内置 Tool 注册 ===
import { quoteTool } from "./quote"
import { accountTool } from "./account"
import { positionTool } from "./position"
import { datetimeTool } from "./datetime"
import { platformTool } from "./platform"
import { knowledgeTool } from "./knowledge"
import { webSearchTool } from "./webSearch"
import { strategyTool } from "./strategy"

const builtinTools: StructuredToolInterface[] = [
  quoteTool,
  accountTool,
  positionTool,
  datetimeTool,
  platformTool,
  knowledgeTool,
  webSearchTool,
  strategyTool,
]

// === 插件 Tool 加载 ===
let pluginToolsCache: StructuredToolInterface[] | null = null

/**
 * 动态加载 plugins/ 目录下的所有 Tool
 * 使用 Vite 的 import.meta.glob 实现
 */
async function loadPluginTools(): Promise<StructuredToolInterface[]> {
  if (pluginToolsCache) return pluginToolsCache

  try {
    // Vite glob 导入
    const modules = import.meta.glob<{ default: StructuredToolInterface | StructuredToolInterface[] }>(
      "./plugins/*.ts",
      { eager: true }
    )

    const tools: StructuredToolInterface[] = []
    for (const [, mod] of Object.entries(modules)) {
      const result = mod.default
      tools.push(...(Array.isArray(result) ? result : [result]))
    }

    pluginToolsCache = tools
    return tools
  } catch (e) {
    // plugins 目录可能不存在或为空
    console.log("[Tools] 无插件 Tool 加载")
    pluginToolsCache = []
    return []
  }
}

/**
 * 获取所有 Tool（内置 + 插件）
 */
export async function getAllTools(): Promise<StructuredToolInterface[]> {
  const plugins = await loadPluginTools()
  return [...builtinTools, ...plugins]
}

/**
 * 获取内置 Tool（同步版本，插件未加载时使用）
 */
export function getBuiltinTools(): StructuredToolInterface[] {
  return [...builtinTools]
}
