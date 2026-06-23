/**
 * 调试节点字段收集工具
 * 
 * 功能：
 * 1. 递归收集上游节点的 output_elements() 字段
 * 2. 连接产生后更新所有后继 DebugNode 的字段选项
 */

import type { Node, Edge } from '@vue-flow/core'

/**
 * 递归收集指定节点的所有上游节点的输出字段
 * 
 * @param nodeId 当前节点 ID
 * @param nodes 所有节点列表
 * @param edges 所有边列表
 * @param visited 已访问节点集合（防止循环）
 * @returns 所有上游输出字段的集合
 */
export function collectUpstreamFields(
  nodeId: string,
  nodes: Node[],
  edges: Edge[],
  visited = new Set<string>()
): Set<string> {
  if (visited.has(nodeId)) return new Set()
  visited.add(nodeId)

  const fields = new Set<string>()
  const node = nodes.find(n => n.id === nodeId)
  if (!node) return fields

  // 收集当前节点的输出字段
  const outputs = node.data?.outputs || {}
  for (const key of Object.keys(outputs)) {
    fields.add(key)
  }

  // 递归收集上游节点
  const upstreamEdges = edges.filter(e => e.target === nodeId)
  for (const edge of upstreamEdges) {
    const upstreamFields = collectUpstreamFields(edge.source, nodes, edges, visited)
    upstreamFields.forEach(f => fields.add(f))
  }

  return fields
}

/**
 * 更新单个 DebugNode 的 outputFields 选项
 * 
 * @param node 调试节点
 * @param nodes 所有节点列表
 * @param edges 所有边列表
 */
export function updateDebugNodeFields(node: Node, nodes: Node[], edges: Edge[]) {
  if (node.data?.nodeType !== 'debug') return

  const upstreamFields = collectUpstreamFields(node.id, nodes, edges)
  const options = Array.from(upstreamFields).map(f => ({ label: f, value: f }))

  if (node.data.params?.outputFields) {
    node.data.params.outputFields.options = options
    
    // 默认全选（如果之前没选过或选项变化）
    if (!node.data.params.outputFields.value?.length && upstreamFields.size > 0) {
      node.data.params.outputFields.value = Array.from(upstreamFields)
    }
  }
}

/**
 * 连接产生后，BFS 遍历所有后继 DebugNode 并更新字段
 * 
 * @param targetNodeId 连接的目标节点 ID
 * @param nodes 所有节点列表
 * @param edges 所有边列表
 */
export function updateAllSuccessorDebugNodes(
  targetNodeId: string,
  nodes: Node[],
  edges: Edge[]
) {
  // BFS 遍历所有后继节点
  const queue = [targetNodeId]
  const visited = new Set<string>()

  while (queue.length > 0) {
    const currentId = queue.shift()!
    if (visited.has(currentId)) continue
    visited.add(currentId)

    const node = nodes.find(n => n.id === currentId)
    if (node?.data?.nodeType === 'debug') {
      updateDebugNodeFields(node, nodes, edges)
    }

    // 加入后继节点
    const successorEdges = edges.filter(e => e.source === currentId)
    for (const edge of successorEdges) {
      if (!visited.has(edge.target)) {
        queue.push(edge.target)
      }
    }
  }
}

/**
 * 收集流程图中所有 DebugNode
 */
export function getAllDebugNodes(nodes: Node[]): Node[] {
  return nodes.filter(n => n.data?.nodeType === 'debug')
}
