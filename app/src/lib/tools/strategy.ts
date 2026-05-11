/**
 * 策略 Tool
 *
 * Agent 可通过此工具：
 * - 查询所有可用节点类型
 * - 获取单个节点的详细信息（参数、输入输出、示例）
 * - 获取当前策略图
 * - 更新/删除策略图
 * - 根据描述创建策略图
 */

import { tool } from "@langchain/core/tools"
import { z } from "zod"
import { getAllNodes, getNode, searchNodes } from "../nodes"

// === 简易策略图存储（IndexedDB 由 history.ts 管理，此处为轻量内存缓存）===

interface SavedStrategy {
  id: string
  name: string
  data: any  // graph JSON
}

const strategies = new Map<string, SavedStrategy>()

/** 获取所有已保存策略 */
function listStrategies(): string {
  if (strategies.size === 0) return "当前没有已保存的策略图"
  const lines = Array.from(strategies.values()).map(s =>
    `- **${s.name}** (id: \`${s.id}\`)`
  )
  return `已保存的策略图（共 ${strategies.size} 个）：\n${lines.join('\n')}`
}

/** 获取单个策略 */
function getStrategy(id: string): string {
  const s = strategies.get(id)
  if (!s) return `未找到策略图 "${id}"`
  return `\`\`\`json\n${JSON.stringify(s.data, null, 2)}\n\`\`\``
}

/** 更新策略图 */
function updateStrategy(id: string, data: any): string {
  const s = strategies.get(id)
  if (!s) return `未找到策略图 "${id}"，请先使用 create_strategy 创建`
  s.data = data
  s.name = data.graph?.name || s.name
  return `策略图 "${s.name}" (${id}) 已更新`
}

/** 删除策略图 */
function deleteStrategy(id: string): string {
  const s = strategies.get(id)
  if (!s) return `未找到策略图 "${id}"`
  strategies.delete(id)
  return `策略图 "${s.name}" (${id}) 已删除`
}

// === 主 Tool ===

export const strategyTool = tool(
  async ({ action, keyword, data, category }) => {
    switch (action) {
      // === 节点查询 ===

      case "list_nodes": {
        const nodes = keyword ? searchNodes(keyword) :
          category ? getAllNodes().filter(n => n.category === category) :
          getAllNodes()

        const lines = nodes.map(n =>
          `- **${n.label}** (id: \`${n.id}\`, type: \`${n.nodeType}\`) — ${n.description}`
        )
        return `可用节点类型（共 ${nodes.length} 个）：\n${lines.join('\n')}`
      }

      case "get_node_info": {
        if (!keyword) return "错误：get_node_info 需要提供 keyword 参数（节点 id 或名称）"
        const n = getNode(keyword) || searchNodes(keyword)[0]
        if (!n) return `未找到节点 "${keyword}"`

        const info = [
          `**${n.label}** (id: \`${n.id}\`, type: \`${n.nodeType}\`)`,
          ``,
          `**描述**: ${n.description}`,
          `**分类**: ${n.category}`,
          `**输入**: ${n.inputs.length ? n.inputs.join(', ') : '无（数据源）'}`,
          `**输出**: ${n.outputs.length ? n.outputs.join(', ') : '无（终端节点）'}`,
          ``,
          `**参数**：`,
          ...n.params.map(p => {
            const opts = p.options
              ? `，可选: ${Array.isArray(p.options) && typeof p.options[0] === 'object' ? p.options.map((o: any) => o.label).join('/') : p.options.join('/')}`
              : ''
            return `  - \`${p.key}\` (${p.label}): ${p.type}，默认值 = ${JSON.stringify(p.default)}${opts}`
          }),
          ``,
          `**示例**：`,
          `\`\`\`json`,
          JSON.stringify(n.example, null, 2),
          `\`\`\``,
        ]
        return info.join('\n')
      }

      // === 策略图 CRUD ===

      case "list_strategies": {
        return listStrategies()
      }

      case "get_strategy": {
        if (!keyword) return "错误：get_strategy 需要提供 keyword 参数（策略 id）"
        return getStrategy(keyword)
      }

      case "create_strategy": {
        if (!data) return "错误：create_strategy 需要提供 data 参数（策略图 JSON）"
        const id = `strategy_${Date.now()}`
        strategies.set(id, { id, name: data.graph?.name || "未命名策略", data })
        return `策略图 "${data.graph?.name || '未命名策略'}" 已创建，id: \`${id}\``
      }

      case "update_strategy": {
        if (!keyword) return "错误：update_strategy 需要提供 keyword 参数（策略 id）"
        if (!data) return "错误：update_strategy 需要提供 data 参数（新策略图 JSON）"
        return updateStrategy(keyword, data)
      }

      case "delete_strategy": {
        if (!keyword) return "错误：delete_strategy 需要提供 keyword 参数（策略 id）"
        return deleteStrategy(keyword)
      }

      default:
        return `未知 action: ${action}。支持的 action: list_nodes, get_node_info, list_strategies, get_strategy, create_strategy, update_strategy, delete_strategy`
    }
  },
  {
    name: "strategy",
    description: "策略管理工具。可查询节点类型信息，以及创建/获取/更新/删除策略图。action='list_nodes' 列出所有可用节点类型；action='get_node_info' 获取单个节点详情（keyword=节点id）；action='list_strategies' 列出已保存策略；action='get_strategy' 获取策略图 JSON（keyword=策略id）；action='create_strategy' 创建新策略图（data=JSON）；action='update_strategy' 更新策略图（keyword=id, data=JSON）；action='delete_strategy' 删除策略图（keyword=id）",
    schema: z.object({
      action: z.enum([
        "list_nodes", "get_node_info",
        "list_strategies", "get_strategy", "create_strategy", "update_strategy", "delete_strategy"
      ]).describe("操作类型"),
      keyword: z.string().optional().describe("节点 id/名称（get_node_info），或策略 id（get/update/delete_strategy），或搜索关键词（list_nodes 时可选）"),
      data: z.any().optional().describe("策略图 JSON 数据（create_strategy / update_strategy 时必填）"),
      category: z.string().optional().describe("节点分类过滤（list_nodes 时可选）: input/process/signal/execution/ml/risk/utility"),
    }),
  }
)
