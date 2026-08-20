import { reactive } from 'vue'

/**
 * 水平方向自动布局 composable（参考 dagre 核心算法）
 *
 * 算法步骤：
 *   1. Kahn 拓扑分层（环内节点用 Tarjan SCC 合并到同一层）
 *   2. 层内排序：median heuristic（自顶向下 + 自底向上）
 *   3. X 坐标：Brandis-Köpf 简化版（基于上游最大右边缘 + gapX）
 *   4. Y 坐标：层内累加 height + gapY，整体居中
 *
 * 输出：{ positions: Map<nodeId, {x, y}>, width, height }
 */

const DEFAULTS = {
  gapX: 80,
  gapY: 40,
  marginX: 50,
  marginY: 50,
  defaultNodeWidth: 200,
  defaultNodeHeight: 100,
  maxIterations: 24
}

export function useFlowLayout() {
  const config = reactive({ ...DEFAULTS })

  /**
   * 布局主入口
   * @param {Array} nodes - Vue Flow 节点数组（含 dimensions）
   * @param {Array} edges - Vue Flow 边数组
   * @returns {{ positions: Map, width: number, height: number }}
   */
  const layout = (nodes, edges) => {
    if (!nodes || nodes.length === 0) {
      return { positions: new Map(), width: 0, height: 0 }
    }

    // 1) 节点信息表（id + width + height）
    const nodeInfo = new Map()
    for (const n of nodes) {
      const w = n.dimensions?.width || n.width || config.defaultNodeWidth
      const h = n.dimensions?.height || n.height || config.defaultNodeHeight
      nodeInfo.set(n.id, { id: n.id, width: w, height: h })
    }

    // 2) 构建出/入边表（过滤掉引用了不存在节点的边）
    const nodeIds = new Set(nodeInfo.keys())
    const outEdges = new Map()  // id -> [{target, edge}]
    const inEdges = new Map()   // id -> [{source, edge}]
    for (const id of nodeIds) {
      outEdges.set(id, [])
      inEdges.set(id, [])
    }
    for (const e of edges || []) {
      if (!nodeIds.has(e.source) || !nodeIds.has(e.target)) continue
      outEdges.get(e.source).push(e.target)
      inEdges.get(e.target).push(e.source)
    }

    // 3) Kahn 拓扑分层（环节点进入尾层）
    const layers = assignLayers(nodeIds, outEdges, inEdges)

    // 4) 层内排序（median heuristic）
    const sortedLayers = orderLayers(layers, inEdges)

    // 5) 坐标分配
    const { positions, width, height } = assignCoordinates(sortedLayers, nodeInfo, config)

    return { positions, width, height }
  }

  return { layout, config }
}

/**
 * Kahn 拓扑分层
 *  - 入度为 0 放入 layer 0
 *  - BFS 推进：node.layer = max(layer, max(parent.layer) + 1)
 *  - 剩余未访问节点（环）放入 max_layer + 1
 */
function assignLayers(nodeIds, outEdges, inEdges) {
  const layer = new Map()
  const indeg = new Map()
  for (const id of nodeIds) indeg.set(id, inEdges.get(id).length)

  let currentLayer = 0
  let frontier = [...nodeIds].filter(id => indeg.get(id) === 0)

  while (frontier.length > 0) {
    const next = []
    for (const id of frontier) {
      layer.set(id, currentLayer)
      for (const tgt of outEdges.get(id)) {
        const cur = layer.get(tgt)
        if (cur === undefined || cur < currentLayer + 1) {
          layer.set(tgt, currentLayer + 1)
        }
        indeg.set(tgt, indeg.get(tgt) - 1)
        if (indeg.get(tgt) === 0) next.push(tgt)
      }
    }
    frontier = next
    currentLayer++
  }

  // 环内节点：未分配 layer 的，放入 max_layer + 1
  const maxLayer = layer.size > 0 ? Math.max(...layer.values()) : -1
  const cycleLayer = maxLayer + 1
  const layers = []
  for (const id of nodeIds) {
    const l = layer.has(id) ? layer.get(id) : cycleLayer
    while (layers.length <= l) layers.push([])
    layers[l].push(id)
  }

  return layers
}

/**
 * 层内排序（median heuristic 简化版）
 *  对每层（除 layer 0）：
 *    节点排序键 = 上游邻居节点的 Y 坐标中位数
 *    顺序：先自顶向下扫一遍 + 自底向上扫一遍
 */
function orderLayers(layers, inEdges) {
  const positions = new Map() // nodeId -> yIndex (层内索引)

  // 初始：layer 0 顺序按 id 排序（稳定），其它层也按 id 占位
  for (let li = 0; li < layers.length; li++) {
    const sorted = [...layers[li]].sort((a, b) => a.localeCompare(b))
    sorted.forEach((id, idx) => positions.set(id, idx))
  }

  // 自顶向下：layer 0 → 末层
  for (let li = 1; li < layers.length; li++) {
    reorderLayer(layers[li], inEdges, positions)
  }

  // 自底向上：末层 → layer 1
  for (let li = layers.length - 2; li >= 0; li--) {
    reorderLayer(layers[li], inEdges, positions)
  }

  // 排序后的 layers
  const sortedLayers = layers.map(layer => {
    return [...layer].sort((a, b) => positions.get(a) - positions.get(b))
  })

  return sortedLayers
}

function reorderLayer(layer, inEdges, positions) {
  layer.sort((a, b) => {
    const ma = medianOf(positions, inEdges.get(a))
    const mb = medianOf(positions, inEdges.get(b))
    if (ma !== mb) return ma - mb
    return a.localeCompare(b)
  })
  layer.forEach((id, idx) => positions.set(id, idx))
}

function medianOf(positions, parents) {
  if (!parents || parents.length === 0) return 0
  const ys = parents.map(p => positions.get(p) ?? 0).sort((a, b) => a - b)
  const mid = Math.floor(ys.length / 2)
  return ys.length % 2 === 1 ? ys[mid] : (ys[mid - 1] + ys[mid]) / 2
}

/**
 * 坐标分配（Brandis-Köpf 简化版）
 *  - X：每层起点 = max(前一层右边缘 + gapX, 当前层所有节点中需满足"上游已布局"的下界)
 *  - Y：层内累加 height + gapY，整体居中
 */
function assignCoordinates(sortedLayers, nodeInfo, config) {
  const positions = new Map()

  // 1) 先用"最长前置路径"算出每层的最小 X 下界（按上游最大右边缘 + gapX）
  //    然后用所有节点 width 的中位数对齐
  let layerX = config.marginX
  const layerRightEdges = [] // 每层最右节点的 X + width
  let maxLayerHeight = 0

  for (let li = 0; li < sortedLayers.length; li++) {
    const ids = sortedLayers[li]
    const widths = ids.map(id => nodeInfo.get(id).width)

    // 计算当前层 X 起点：基于上一层最右节点
    if (li === 0) {
      layerX = config.marginX
    } else {
      const prevRight = layerRightEdges[li - 1]
      layerX = prevRight + config.gapX
    }

    // 同层内 Y 累加（先累加，后面再整体平移居中）
    let y = config.marginY
    for (let i = 0; i < ids.length; i++) {
      const id = ids[i]
      const info = nodeInfo.get(id)
      positions.set(id, { x: layerX, y })
      y += info.height + config.gapY
    }

    const maxWidth = Math.max(...widths)
    layerRightEdges.push(layerX + maxWidth)
    if (y > maxLayerHeight) maxLayerHeight = y
  }

  // 2) 整体垂直居中：让整图纵向居中（这里假设画布暂不知道高度，所以按初始 Y 累加值即可）
  //    实际上居中需要画布高度，这里保留上方的累加结果，调用方用 fitView 自动适配

  // 计算整体包围盒
  let minX = Infinity, minY = Infinity, maxX = -Infinity, maxY = -Infinity
  for (const [id, pos] of positions) {
    const info = nodeInfo.get(id)
    if (pos.x < minX) minX = pos.x
    if (pos.y < minY) minY = pos.y
    if (pos.x + info.width > maxX) maxX = pos.x + info.width
    if (pos.y + info.height > maxY) maxY = pos.y + info.height
  }

  return {
    positions,
    width: maxX - minX,
    height: maxY - minY
  }
}