import { message } from '@/tool'
import { useHistoryStore } from '@/stores/history'
import { keyMap } from '@/components/flow/nodeConfigs'

/**
 * 导出文件格式标识
 */
const EXPORT_FORMAT = 'quasarx-strategy'
const EXPORT_VERSION = '1.0'

/**
 * 导出当前策略为 JSON 文件
 * @param {string} strategyId - 策略 ID
 * @param {Array} strategies - 策略列表
 * @param {Array} versions - 版本列表
 * @param {Object} historyStore - history store 实例
 */
export async function exportStrategy(strategyId, strategies, versions, historyStore) {
  try {
    // 1. 查找策略
    const strategy = strategies.find(s => s.id === strategyId)
    if (!strategy) {
      message.error('未找到要导出的策略')
      return
    }

    // 2. 获取该策略的所有版本
    const strategyVersions = versions.filter(v => v.strategyId === strategyId)
    if (strategyVersions.length === 0) {
      message.warning('该策略暂无版本数据，请先保存策略')
      return
    }

    // 3. 收集所有版本的流程图数据和回测结果
    const exportedVersions = []
    for (const version of strategyVersions) {
      const flowData = await historyStore.loadVersionFlowData(version.id)
      const backtestResult = await historyStore.loadBacktestResult(version.id)

      exportedVersions.push({
        id: version.id,
        name: version.name || version.id,
        remark: version.remark,
        saveTime: version.saveTime,
        flowData: flowData || null,
        backtestResult: backtestResult || null
      })
    }

    // 4. 构建导出对象
    const exportData = {
      format: EXPORT_FORMAT,
      version: EXPORT_VERSION,
      exportTime: new Date().toISOString(),
      name: strategy.name,
      strategyCreatedAt: strategy.createdAt,
      versions: exportedVersions
    }

    // 5. 生成 JSON 并触发下载
    const jsonStr = JSON.stringify(exportData, null, 2)
    const blob = new Blob([jsonStr], { type: 'application/json' })
    const url = URL.createObjectURL(blob)
    
    const a = document.createElement('a')
    a.href = url
    a.download = `${strategy.name}_${new Date().toISOString().slice(0, 10)}.json`
    document.body.appendChild(a)
    a.click()
    document.body.removeChild(a)
    URL.revokeObjectURL(url)

    message.success(`策略 "${strategy.name}" 已导出`)
    console.info(`[exportStrategy] 策略导出成功：${strategy.name}, ${exportedVersions.length} 个版本`)
  } catch (error) {
    console.error('[exportStrategy] 导出失败:', error)
    message.error('导出失败：' + (error.message || '未知错误'))
  }
}

/**
 * 检测是否为后端配置格式（如 CTA.json）
 * @param {Object} data - 解析后的 JSON 数据
 * @returns {boolean}
 */
function isBackendFormat(data) {
  return !data.format && data.nodes && Array.isArray(data.nodes) && data.name
}

/**
 * 将节点 params 中的英文 key 转换为中文 key
 */
function convertNodeKeys(nodes) {
  return nodes.map(node => {
    if (!node.data?.params) return node

    const convertedParams = {}
    for (const key in node.data.params) {
      const cnKey = keyMap[key] || key
      convertedParams[cnKey] = node.data.params[key]
    }

    return {
      ...node,
      data: {
        ...node.data,
        params: convertedParams
      }
    }
  })
}

/**
 * 将后端边的 handle ID 转换为前端格式
 * 后端: sourceHandle 为 "nodeId-fieldName" 或 "nodeId"，targetHandle 为 "nodeId"
 * 前端: 字段输出 handle 为 "field-xxx"，普通输出 handle 为 "output"，输入 handle 为 "input"
 */
function normalizeEdgeHandles(edges, nodes) {
  // 找出 input 类型节点的 ID
  const inputNodeIds = new Set(
    nodes.filter(n => n.data?.nodeType === 'input').map(n => n.id)
  )

  return edges.map(edge => {
    let { sourceHandle, targetHandle } = edge

    // 转换 sourceHandle
    if (sourceHandle) {
      if (sourceHandle.includes('-')) {
        // "1-close" -> "field-close"
        const fieldName = sourceHandle.split('-').slice(1).join('-')
        sourceHandle = `field-${fieldName}`
      } else {
        // "2" -> "output"
        sourceHandle = 'output'
      }
    }

    // 转换 targetHandle -> "input"
    if (targetHandle) {
      targetHandle = 'input'
    }

    return { ...edge, sourceHandle, targetHandle }
  })
}

/**
 * 将后端配置格式转换为前端导入格式
 * @param {Object} data - 后端配置数据
 * @returns {Object} 转换后的前端格式
 */
function convertBackendToFrontend(data) {
  const now = new Date().toISOString()
  const convertedNodes = convertNodeKeys(data.nodes || [])
  const normalizedEdges = normalizeEdgeHandles(data.edges || [], convertedNodes)

  return {
    format: EXPORT_FORMAT,
    version: EXPORT_VERSION,
    exportTime: now,
    name: data.name,
    strategyCreatedAt: now,
    versions: [{
      id: `${data.id}_v1`,
      name: data.name,
      remark: data.description || '',
      saveTime: now,
      flowData: { nodes: convertedNodes, edges: normalizedEdges },
      backtestResult: null
    }]
  }
}

/**
 * 处理导入数据（核心逻辑）
 * @param {Object} data - 已解析的 JSON 数据
 * @param {Object} historyStore - history store 实例
 * @returns {string|null} 新创建的策略 ID，失败返回 null
 */
async function processImportData(data, historyStore) {
  // 自动识别并转换后端格式
  if (isBackendFormat(data)) {
    console.info('[importStrategy] 检测到后端配置格式，自动转换')
    data = convertBackendToFrontend(data)
  }

  // 验证格式
  if (!validateStrategyFormat(data)) {
    message.error('文件格式不正确，无法导入')
    return null
  }

  // 检查策略名是否冲突，冲突则自动重命名
  let strategyName = data.name
  const existingStrategy = historyStore.strategies.find(s => s.name === strategyName)
  if (existingStrategy) {
    strategyName = `${strategyName}_${Date.now()}`
    console.info(`[importStrategy] 策略名冲突，自动重命名为：${strategyName}`)
  }

  // 创建策略
  const newStrategyId = await historyStore.addStrategy(strategyName)

  // 导入所有版本
  let importedCount = 0
  for (const versionData of data.versions) {
    try {
      // 创建版本
      const newVersionId = await historyStore.addVersion(
        newStrategyId,
        versionData.flowData || undefined,
        versionData.remark
      )

      // 保存回测结果（如果有）
      if (versionData.backtestResult) {
        await historyStore.saveBacktestResult(newVersionId, versionData.backtestResult)
      }

      importedCount++
    } catch (versionError) {
      console.error(`[importStrategy] 导入版本 ${versionData.id} 失败:`, versionError)
    }
  }

  // 刷新策略列表
  await historyStore.initialize()

  message.success(`策略 "${strategyName}" 已导入，共 ${importedCount} 个版本`)
  console.info(`[importStrategy] 策略导入成功：${strategyName}, ${importedCount}/${data.versions.length} 个版本`)

  return newStrategyId
}

/**
 * 从 JSON 文件导入策略
 * @param {File} file - JSON 文件
 * @param {Object} historyStore - history store 实例
 * @returns {string|null} 新创建的策略 ID，失败返回 null
 */
export async function importStrategy(file, historyStore) {
  try {
    // 1. 读取文件
    const text = await file.text()
    const data = JSON.parse(text)

    // 2. 处理导入
    return await processImportData(data, historyStore)
  } catch (error) {
    console.error('[importStrategy] 导入失败:', error)
    message.error('导入失败：' + (error.message || '文件格式错误'))
    return null
  }
}

/**
 * 验证导入数据格式
 * @param {Object} data - 解析后的 JSON 数据
 * @returns {boolean} 是否有效
 */
function validateStrategyFormat(data) {
  // 检查格式标识
  if (data.format !== EXPORT_FORMAT) {
    console.warn('[validateStrategyFormat] 格式标识不匹配')
    return false
  }

  // 检查版本号
  if (!data.version) {
    console.warn('[validateStrategyFormat] 缺少版本号')
    return false
  }

  // 检查策略名称
  if (!data.name || typeof data.name !== 'string') {
    console.warn('[validateStrategyFormat] 缺少策略名称')
    return false
  }

  // 检查版本列表
  if (!Array.isArray(data.versions) || data.versions.length === 0) {
    console.warn('[validateStrategyFormat] 缺少版本数据')
    return false
  }

  // 检查每个版本的基本结构
  for (const version of data.versions) {
    if (!version.id || !version.remark) {
      console.warn('[validateStrategyFormat] 版本数据结构不完整')
      return false
    }
  }

  return true
}
