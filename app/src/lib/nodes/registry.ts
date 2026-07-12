/**
 * 节点注册中心
 *
 * 所有节点类型在此注册。UI 和 AI Tool 都从注册表读取信息，
 * 新节点只需在此注册即可同时被 UI 和 AI 识别。
 */


import type { NodeRegistryEntry, NodeCategory } from './types'
import { CATEGORY_ICONS } from './types'

// === 注册表 ===

const registry = new Map<string, NodeRegistryEntry>()

/** 注册一个节点类型 */
export function registerNode(entry: NodeRegistryEntry): void {
  registry.set(entry.id, entry)
}

/** 根据 ID 获取节点配置 */
export function getNode(id: string): NodeRegistryEntry | undefined {
  return registry.get(id)
}

/** 获取所有节点配置 */
export function getAllNodes(): NodeRegistryEntry[] {
  return Array.from(registry.values())
}

/** 按分类获取节点 */
export function getNodesByCategory(category: NodeCategory): NodeRegistryEntry[] {
  return getAllNodes().filter(n => n.category === category)
}

/** 获取所有分类（去重排序） */
export function getAllCategories(): NodeCategory[] {
  const cats = new Set(getAllNodes().map(n => n.category))
  // 固定排序
  const order: NodeCategory[] = ['input', 'process', 'signal', 'execution', 'ml', 'risk', 'causal', 'utility']
  return order.filter(c => cats.has(c))
}

/** 搜索节点（按 label / nodeType / description 模糊匹配） */
export function searchNodes(query: string): NodeRegistryEntry[] {
  const q = query.toLowerCase()
  return getAllNodes().filter(n =>
    n.label.toLowerCase().includes(q) ||
    n.nodeType.toLowerCase().includes(q) ||
    n.description.toLowerCase().includes(q)
  )
}

/**
 * 将策略图 JSON 中的中文参数键名转换为英文键名
 * 用于发送给后端 C++ 服务
 */
export function convertLabelsToKeys(graphJson: string): string {
  let result = graphJson
  
  // 定义参数别名映射（处理旧版本或用户修改的标签）
  const paramAliases: Record<string, string> = {
    '范围': 'range',
    '窗口': 'range',
    '方法': 'method',
  }
  
  // 先处理别名转换
  for (const [alias, key] of Object.entries(paramAliases)) {
    const regex = new RegExp(`"${alias.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')}"\\s*:`, 'g')
    result = result.replace(regex, `"${key}":`)
  }
  
  // 再处理注册表中的标准参数
  for (const node of getAllNodes()) {
    for (const p of node.params) {
      // 用 "label": 模式精确匹配，避免子串替换
      const regex = new RegExp(`"${p.label.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')}"\\s*:`, 'g')
      result = result.replace(regex, `"${p.key}":`)
    }
  }
  
  return result
}

/**
 * 将后端返回的英文键名转换为中文参数键名（UI 显示用）
 */
export function convertKeysToLabels(params: Record<string, any>): Record<string, any> {
  const result: Record<string, any> = {}
  for (const [key, value] of Object.entries(params)) {
    // 在注册表中查找匹配的 key
    let foundLabel = key // 默认使用原 key
    for (const node of getAllNodes()) {
      for (const p of node.params) {
        if (p.key === key) {
          foundLabel = p.label
          break
        }
      }
      if (foundLabel !== key) break
    }
    result[foundLabel] = value
  }
  return result
}

/** 获取节点图标（使用分类默认图标） */
export function getNodeIcon(label: string): string {
  for (const n of getAllNodes()) {
    if (n.label === label) return CATEGORY_ICONS[n.category] || 'fas fa-cube'
  }
  return 'fas fa-cube'
}

/** 获取节点图标（按节点 ID 查找） */
export function getNodeIconById(id: string): string {
  const n = registry.get(id)
  return n ? CATEGORY_ICONS[n.category] || 'fas fa-cube' : 'fas fa-cube'
}

/** 获取节点颜色（兼容旧接口，按 label 查找） */
export function getNodeColor(label: string): string | undefined {
  for (const n of getAllNodes()) {
    if (n.label === label) return n.color
  }
  return undefined
}
