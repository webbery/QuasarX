/**
 * 代码同步与格式化 composable
 * 处理股票代码格式转换、节点间代码同步、键名映射等
 */

/**
 * 将股票代码转换为带交易所前缀的格式
 * @param {string|string[]} code - 股票代码
 * @returns {string|string[]} 格式化后的代码 (如: 000001 -> sz.000001)
 */
export function normalizeCode(code) {
  if (Array.isArray(code)) {
    return code.map(c => normalizeOne(c))
  }
  return normalizeOne(code)
}

function normalizeOne(code) {
  code = code.trim()
  // 如果已经包含交易所前缀，直接返回
  if (code.includes('.')) {
    return code.toLowerCase()
  }
  // 根据股票代码规则判断交易所
  const firstDigit = code.charAt(0)
  const firstTwo = code.substring(0, 2)
  const firstThree = code.substring(0, 3)

  if (firstDigit === '6' || firstThree === '688') {
    return 'sh.' + code
  } else if (firstDigit === '0' || firstDigit === '3') {
    return 'sz.' + code
  } else if (firstDigit === '4' || firstDigit === '8') {
    return 'bj.' + code
  } else if (firstTwo === '51' || firstTwo === '52') {
    // ETF 基金
    return 'sh.' + code
  } else if (firstTwo === '15' || firstTwo === '16') {
    return 'sz.' + code
  }
  // 默认返回 sz
  return 'sz.' + code
}

/**
 * 从 input 节点出发，沿连接方向查找所有可达的 signal 节点（BFS 遍历）
 * @param {string} inputNodeId - 输入节点 ID
 * @param {Array} edges - 边数组
 * @param {Array} nodes - 节点数组
 * @returns {Array} signal 节点数组
 */
export function findSignalNodesFromInput(inputNodeId, edges, nodes) {
  const signalNodes = []
  const visited = new Set()
  const queue = [inputNodeId]

  while (queue.length > 0) {
    const currentId = queue.shift()
    if (visited.has(currentId)) continue
    visited.add(currentId)

    // 找到从当前节点出发的所有边
    const outgoingEdges = edges.filter(e => e.source === currentId)
    for (const edge of outgoingEdges) {
      const targetId = edge.target
      if (!visited.has(targetId)) {
        const targetNode = nodes.find(n => n.id === targetId)
        if (targetNode) {
          if (targetNode.data.nodeType === 'signal') {
            signalNodes.push(targetNode)
          }
          // 继续遍历下游节点
          queue.push(targetId)
        }
      }
    }
  }

  return signalNodes
}

/**
 * 同步 code 从 input 节点到所有可达的 signal 节点
 * @param {Object} inputNode - 输入节点
 * @param {Array} edges - 边数组
 * @param {Array} nodes - 节点数组
 * @param {Function} updateNodeData - 更新节点数据的回调
 */
export function syncCodeToDownstreamSignals(inputNode, edges, nodes, updateNodeData) {
  const codeValue = inputNode.data.params['代码']?.value
  if (!codeValue) return

  // 将 code 转换为带交易所前缀的格式
  const normalizedCode = normalizeCode(codeValue)

  const signalNodes = findSignalNodesFromInput(inputNode.id, edges, nodes)
  for (const signalNode of signalNodes) {
    updateNodeData(signalNode.id, '代码', normalizedCode)
  }
}

/**
 * 替换对象键名的辅助函数
 * @param {Object} obj - 要处理的对象
 * @param {Object} keyMapping - 键名映射
 */
export function replaceKeysInObject(obj, keyMapping) {
  Object.keys(obj).forEach(key => {
    // 如果当前键在映射中存在，则进行替换
    if (keyMapping[key]) {
      const newKey = keyMapping[key]
      // 只有当新键名与旧键名不同时才进行替换
      if (newKey !== key) {
        obj[newKey] = obj[key]
        delete obj[key]
      }
    }

    // 递归处理嵌套对象
    if (typeof obj[key] === 'object' && obj[key] !== null && !Array.isArray(obj[key])) {
      replaceKeysInObject(obj[key], keyMapping)
    }
  })
}

/**
 * 将节点参数的中文键名转换为服务器端键名
 * @param {Object} json_data - JSON 数据
 */
export function toServerKey(json_data, keyMap) {
  const reverseKeyMap = Object.fromEntries(
    Object.entries(keyMap).map(([key, value]) => [value, key])
  )

  // 替换节点中的关键字
  if (json_data.graph?.nodes) {
    json_data.graph.nodes.forEach(node => {
      if (node.data?.params) {
        replaceKeysInObject(node.data.params, reverseKeyMap)
      }
    })
  }
  // 删除 edge 中的无用字段
  if (json_data.graph?.edges) {
    json_data.graph.edges.forEach(edge => {
      delete edge.markerEnd
      delete edge.style
    })
  }
}
