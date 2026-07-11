/**
 * 代码同步与格式化 composable
 * 处理股票代码格式转换、键名映射等
 * 
 * 注意：标的代码统一从 QuoteInputNode 传递，SignalNode 不再存储 code 参数。
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
