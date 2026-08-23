import { message } from '@/tool'
import { createStrategyNode } from '@/lib/nodes'
import { functionInputSlots, getFunctionInputHandleId, getSlotInputHandleId } from '@/lib/nodes/configs/function'
import { ipcRenderer } from 'electron'

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
 * 将后端边的 handle ID 转换为前端格式
 *
 * sourceHandle 转换:
 *   "1-close"         → "field-close"                  (QuoteInput 字段输出)
 *   "11-IMF_0"        → "field-IMF" + data.imfIndex=0  (EMD 命名输出，老格式)
 *   "11-IMF"          → "field-IMF" + data.imfIndex 透传 (EMD 命名输出，新格式)
 *   "11-energy_velocity" → "field-energy_velocity"      (EMD 衍生特征)
 *   "11-volume_regime"   → "field-volume_regime"        (EMD 衍生特征)
 *   "5-hmm_state"     → "field-hmm_state"              (HMM 输出)
 *   "2"               → "output"                       (通用单输出)
 *
 * targetHandle 转换:
 *   BreakoutNode: 保留 "input-value/upper/lower"
 *   FunctionNode:  根据 sourceHandle 字段名映射到 "input-{slot}"
 *   其他节点:      → "input"
 */
function normalizeEdgeHandles(edges, nodes) {
  // 构建节点 ID → nodeType 映射
  const nodeTypeMap = {}
  const nodeMethodMap = {}
  for (const n of nodes) {
    nodeTypeMap[n.id] = n.data?.nodeType || ''
    // FunctionNode 需要知道 method 来确定槽位
    if (n.data?.nodeType === 'function') {
      // 优先英文 key，兜底中文 label（旧数据兼容）
      const params = n.data?.params || {}
      const method = params['method']?.value || params['方法']?.value
        || Object.values(params).find((c) => c && c.type === 'select' && Array.isArray(c.options) && c.options.some((o) => o && o.value === 'VPCorr'))?.value
        || 'MA'
      nodeMethodMap[n.id] = method
    }
  }

  return edges.map(edge => {
    let { sourceHandle, targetHandle } = edge
    const targetNodeType = nodeTypeMap[edge.target] || ''
    const sourceNodeType = nodeTypeMap[edge.source] || ''
    let edgeData = edge.data ? { ...edge.data } : null

    // ── 转换 sourceHandle ──
    if (sourceHandle) {
      if (sourceHandle.includes('-')) {
        const fieldName = sourceHandle.split('-').slice(1).join('-')
        if (sourceNodeType === 'emd') {
          // EMD 命名输出: "11-IMF_0" / "11-IMF" → "field-IMF"
          if (fieldName === 'IMF' || fieldName.startsWith('IMF_')) {
            sourceHandle = 'field-IMF'
            // 老格式 "11-IMF_0" → 从 IMF_0 后缀提取 imfIndex
            if (fieldName.startsWith('IMF_')) {
              const idx = parseInt(fieldName.slice(4), 10)
              if (!isNaN(idx) && idx >= 0) {
                edgeData = { ...(edgeData || {}), imfIndex: idx }
              }
            }
            // 新格式 "11-IMF" → edge.data.imfIndex 已在原数据中，保留
          } else {
            // EMD 衍生特征: "11-energy_velocity" → "field-energy_velocity"
            sourceHandle = `field-${fieldName}`
          }
        } else if (sourceNodeType === 'input') {
          // QuoteInput 字段输出: "1-close" → "field-close"
          sourceHandle = `field-${fieldName}`
        } else {
          // 其他节点多输出（HMM 等）: "5-hmm_state" → "field-hmm_state"
          sourceHandle = `field-${fieldName}`
        }
      } else {
        // 通用单输出: "2" → "output"
        sourceHandle = 'output'
      }
    }

    // ── 转换 targetHandle ──
    if (targetHandle) {
      if (targetNodeType === 'breakout') {
        // BreakoutNode: 保留命名 handle "input-value/upper/lower"
        if (!targetHandle.startsWith('input-')) {
          targetHandle = 'input-value' // 默认
        }
      } else if (targetNodeType === 'function') {
        // FunctionNode: 与渲染共享同一工具函数
        // - 单输入方法（MA/STD/...）：getFunctionInputHandleId 直接给出首槽 id
        // - 多输入方法（VPCorr/ATR）：按源字段名匹配槽位，再走 getSlotInputHandleId
        const method = nodeMethodMap[edge.target] || 'MA'
        const slots = functionInputSlots[method] || functionInputSlots['MA'] || []

        if (slots.length <= 1) {
          targetHandle = getFunctionInputHandleId(method)
        } else {
          // 从原始 sourceHandle 提取字段名
          let srcField = ''
          if (edge.sourceHandle && edge.sourceHandle.includes('-')) {
            srcField = edge.sourceHandle.split('-').slice(1).join('-')
          }
          const matchedSlot = srcField ? slots.find(s => s.field === srcField) : null
          targetHandle = getSlotInputHandleId(matchedSlot || slots[0])
        }
      } else {
        // 其他节点: 单一输入
        targetHandle = 'input'
      }
    }

    return edgeData ? { ...edge, sourceHandle, targetHandle, data: edgeData } : { ...edge, sourceHandle, targetHandle }
  })
}

/**
 * 将后端配置格式转换为前端导入格式
 * @param {Object} data - 后端配置数据
 * @returns {Object} 转换后的前端格式
 */
function convertBackendToFrontend(data) {
  const now = new Date().toISOString()
  const backendNodes = data.nodes || []
  const normalizedEdges = normalizeEdgeHandles(data.edges || [], backendNodes)

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
      flowData: { nodes: backendNodes, edges: normalizedEdges },
      backtestResult: null
    }]
  }
}

/**
 * 从 modelFile 路径提取原策略名和 label
 * 格式: production/{strategyName}-{label}.json
 * @param {string} modelFile
 * @returns {{ origStrategy: string, label: string } | null}
 */
function extractModelFileInfo(modelFile) {
  if (!modelFile || !modelFile.startsWith('production/')) return null
  const basename = modelFile.replace('production/', '').replace(/\.json$/, '')
  const lastDash = basename.lastIndexOf('-')
  if (lastDash <= 0) return null
  return { origStrategy: basename.slice(0, lastDash), label: basename.slice(lastDash + 1) }
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

  for (const versionData of data.versions) {
    if (!versionData.flowData) continue
    if (!Array.isArray(versionData.flowData.nodes)) {
      throw new Error(`版本 ${versionData.id} 缺少 nodes 数组`)
    }

    versionData.flowData = {
      ...versionData.flowData,
      nodes: versionData.flowData.nodes.map(node => {
        const created = createStrategyNode({
          id: node.id,
          nodeType: node.data?.nodeType,
          position: node.position,
          label: node.data?.label,
          params: node.data?.params,
        })
        if (created.data?.nodeType === 'xgboost') {
          console.info(`[importStrategy] createStrategyNode 后 XGBoost[${created.id}] modelFile:`, JSON.stringify(created.data.params?.modelFile))
        }
        return created
      })
    }
  }

  // 检查策略名是否冲突，冲突则自动重命名
  let strategyName = data.name
  const existingStrategy = historyStore.strategies.find(s => s.name === strategyName)
  if (existingStrategy) {
    strategyName = `${strategyName}_${Date.now()}`
    console.info(`[importStrategy] 策略名冲突，自动重命名为：${strategyName}`)
  }

  // 校验 XGBoost 节点模型文件是否存在，不存在则清空绑定
  console.info(`[importStrategy] 开始校验 XGBoost 模型绑定, strategyName=${strategyName}, versions=${data.versions.length}`)
  for (const versionData of data.versions) {
    if (!versionData.flowData?.nodes) continue
    for (const node of versionData.flowData.nodes) {
      if (node.data?.nodeType !== 'xgboost') continue
      const modelFile = node.data.params?.modelFile?.value || ''
      console.info(`[importStrategy] XGBoost[${node.id}] 校验前 modelFile="${modelFile}"`)
      if (!modelFile) {
        console.info(`[importStrategy] XGBoost[${node.id}] modelFile 为空，跳过`)
        continue
      }

      const info = extractModelFileInfo(modelFile)
      console.info(`[importStrategy] XGBoost[${node.id}] extractModelFileInfo:`, JSON.stringify(info))
      if (!info) {
        node.data.params.modelFile.value = ''
        console.info(`[importStrategy] XGBoost[${node.id}] modelFile 格式不合法，已清空`)
        continue
      }

      // 1. 检查新策略名下是否已有模型
      let checkRes
      try {
        checkRes = await ipcRenderer.invoke('model-check-exists', {
          strategyName, label: info.label
        })
        console.info(`[importStrategy] XGBoost[${node.id}] model-check-exists 结果:`, JSON.stringify(checkRes))
      } catch (err) {
        console.error(`[importStrategy] XGBoost[${node.id}] model-check-exists IPC 调用失败:`, err.message)
        checkRes = null
      }
      if (checkRes?.exists) {
        node.data.params.modelFile.value = `production/${strategyName}-${info.label}.json`
        console.info(`[importStrategy] XGBoost[${node.id}] 模型已存在，更新为: ${node.data.params.modelFile.value}`)
        continue
      }

      // 2. 新策略名没有 → 尝试从原策略名复制
      if (info.origStrategy && info.origStrategy !== strategyName) {
        let copyRes
        try {
          copyRes = await ipcRenderer.invoke('model-copy-between', {
            fromStrategy: info.origStrategy,
            toStrategy: strategyName,
            label: info.label
          })
          console.info(`[importStrategy] XGBoost[${node.id}] model-copy-between 结果:`, JSON.stringify(copyRes))
        } catch (err) {
          console.error(`[importStrategy] XGBoost[${node.id}] model-copy-between IPC 调用失败:`, err.message)
          copyRes = null
        }
        if (copyRes?.success) {
          node.data.params.modelFile.value = `production/${strategyName}-${info.label}.json`
          console.info(`[importStrategy] XGBoost[${node.id}] 模型已从原策略复制，更新为: ${node.data.params.modelFile.value}`)
          continue
        }
      }

      // 3. 都不存在 → 清空绑定
      node.data.params.modelFile.value = ''
      console.info(`[importStrategy] XGBoost[${node.id}] 模型不存在，已清空绑定 → modelFile=""`)
    }
  }

  // 创建策略
  const newStrategyId = await historyStore.addStrategy(strategyName)

  // 导入所有版本
  let importedCount = 0
  for (const versionData of data.versions) {
    try {
      // 保存前最终确认 XGBoost modelFile 状态
      if (versionData.flowData?.nodes) {
        for (const node of versionData.flowData.nodes) {
          if (node.data?.nodeType === 'xgboost') {
            const mf = node.data.params?.modelFile?.value || ''
            console.info(`[importStrategy] 保存前最终检查 XGBoost[${node.id}] modelFile="${mf}"`)
          }
        }
      }

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
