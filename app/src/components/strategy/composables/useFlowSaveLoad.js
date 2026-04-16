import { nextTick } from 'vue'
import { MarkerType } from '@vue-flow/core'
import { message } from '@/tool'
import { useHistoryStore } from '@/stores/history'
import { storeToRefs } from 'pinia'

/**
 * 流程图保存/加载 composable
 * 处理策略和版本的保存、加载、创建等操作
 */
export function useFlowSaveLoad(state, operations) {
  const {
    getNodes,
    getEdges,
    removeNodes,
    removeEdges,
    addNodes,
    addEdges,
    fitView,
    currentStrategyId,
    currentVersionId,
    hasUnsavedChanges,
    nodeIdCounter,
    selectedNodes,
    selectedEdges
  } = state

  // Pinia Store
  const historyStore = useHistoryStore()
  const { strategies, versions } = storeToRefs(historyStore)

  const { updateNodeData } = operations

  // localStorage 键名
  const FLOW_STORAGE_KEY = 'vue-flow-saved-strategy'

  /**
   * 格式化日期为备注字符串
   */
  const formatDateForRemark = (date) => {
    const year = date.getFullYear()
    const month = String(date.getMonth() + 1).padStart(2, '0')
    const day = String(date.getDate()).padStart(2, '0')
    const hour = String(date.getHours()).padStart(2, '0')
    const minute = String(date.getMinutes()).padStart(2, '0')
    return `${year}-${month}-${day} ${hour}:${minute}`
  }

  /**
   * 创建新版本
   */
  const createNewVersion = async (remark, promptDialogRef, addInfoMessage) => {
    if (!currentStrategyId.value) {
      message.error('未关联任何策略，无法保存版本')
      return
    }

    const flowData = {
      nodes: getNodes.value,
      edges: getEdges.value
    }

    try {
      const versionId = await historyStore.addVersion(
        currentStrategyId.value,
        flowData,
        remark
      )
      currentVersionId.value = versionId
      hasUnsavedChanges.value = false
      message.success('版本保存成功')
      if (addInfoMessage) {
        addInfoMessage('版本保存成功', 'success')
      }
    } catch (error) {
      console.error('[createNewVersion] 保存失败:', error)
      message.error('保存失败')
    }
  }

  /**
   * 保存流程图
   */
  const saveFlow = async (promptDialogRef, addInfoMessage) => {
    if (currentStrategyId.value) {
      // 已有策略 → 直接保存为新版本（使用默认备注：当前时间）
      createNewVersion(undefined, promptDialogRef, addInfoMessage)
    } else {
      // 无策略 → 先新建策略，再保存版本
      const result = await promptDialogRef?.show({
        title: '新建策略',
        placeholder: '请输入策略名称'
      })
      if (result?.cancelled) return
      if (!result?.value || !result.value.trim()) {
        message.warning('策略名称不能为空')
        return
      }
      // 调用 store 添加策略
      const newStrategyId = await historyStore.addStrategy(result.value.trim())
      currentStrategyId.value = newStrategyId
      // 保存为新版本（默认备注）
      createNewVersion(undefined, promptDialogRef, addInfoMessage)
    }
  }

  /**
   * 另存为新版本（弹出备注输入框）
   */
  const saveAsNewVersion = async (promptDialogRef, addInfoMessage) => {
    // 如果无策略，先新建
    if (!currentStrategyId.value) {
      const result = await promptDialogRef?.show({
        title: '新建策略',
        placeholder: '请输入策略名称'
      })
      if (result?.cancelled) return
      if (!result?.value || !result.value.trim()) return

      const newStrategyId = await historyStore.addStrategy(result.value.trim())
      currentStrategyId.value = newStrategyId
    }

    // 弹出备注输入框
    const defaultRemark = formatDateForRemark(new Date())
    const result = await promptDialogRef?.show({
      title: '版本备注',
      placeholder: '请输入版本备注（留空将使用当前时间）',
      defaultValue: defaultRemark
    })
    if (result?.cancelled) return
    createNewVersion(result.value.trim() || undefined, promptDialogRef, addInfoMessage)
  }

  /**
   * 从 localStorage 加载保存的流程图
   */
  const loadSavedFlow = async () => {
    try {
      const savedData = localStorage.getItem(FLOW_STORAGE_KEY)
      if (savedData) {
        const parsedData = JSON.parse(savedData)
        let loadedNodes = parsedData.nodes || []
        let loadedEdges = parsedData.edges || []
        // 首先清空现有数据
        removeNodes(getNodes.value.map(n => n.id))
        removeEdges(getEdges.value.map(e => e.id))

        // 先添加节点，确保节点存在
        await nextTick(() => {
          addNodes(loadedNodes.map(node => ({
            ...node,
            // 确保节点类型正确
            type: node.type || 'custom'
          })))
        })

        // 更新节点计数器
        if (loadedNodes.length > 0) {
          const maxId = Math.max(...loadedNodes.map(node => parseInt(node.id) || 0))
          nodeIdCounter.value = maxId + 1
        }
        // 然后添加边（使用 nextTick 确保节点已添加）
        nextTick(() => {
          // 验证并添加边
          const validEdges = loadedEdges.filter(edge => {
            const sourceExists = getNodes.value.some(n => n.id === edge.source)
            const targetExists = getNodes.value.some(n => n.id === edge.target)
            if (!sourceExists || !targetExists) {
              console.warn(`边 ${edge.id} 引用了不存在的节点: source=${edge.source}, target=${edge.target}`)
              return false
            }
            return true
          })
          const curEdges = validEdges.map(edge => ({
            ...edge,
            source: String(edge.source),
            target: String(edge.target),
            // 确保边类型正确
            type: edge.type || 'default',
            // 确保 markerEnd 存在
            markerEnd: edge.markerEnd || {
              type: MarkerType.ArrowClosed,
              color: 'var(--primary)',
            },
            // 确保 style 存在
            style: edge.style || {
              stroke: 'var(--primary)',
              strokeWidth: 2,
            }
          }))
          addEdges(curEdges)

          message.success('已加载保存的流程图')

          // 重新适应视图
          setTimeout(() => {
            fitView({ padding: 0.25 })
          }, 100)
        })
      }
    } catch (error) {
      console.error('加载流程图数据失败:', error)
      message.error('加载流程图数据失败')
    }
  }

  /**
   * 验证流程图有效性
   */
  const validateFlow = () => {
    const nodes = getNodes.value
    if (nodes.length === 0) {
      message.error('流程图为空，请添加节点')
      return false
    }

    const hasSignalNode = nodes.some(n => n.data.nodeType === 'signal')
    if (!hasSignalNode) {
      message.error('缺少信号节点，请添加至少一个信号输出节点')
      return false
    }

    const hasDataNode = nodes.some(n => n.data.nodeType === 'input')
    if (!hasDataNode) {
      message.error('缺少数据节点，请添加数据输入节点')
      return false
    }

    return true
  }

  /**
   * 确保有版本 ID（用于保存回测结果）
   */
  const ensureVersionId = async (message) => {
    // 如果已有版本 ID，直接返回
    if (currentVersionId.value) {
      return currentVersionId.value
    }

    // 如果有策略 ID 但没有版本 ID，创建临时版本
    if (currentStrategyId.value) {
      const flowData = {
        nodes: getNodes.value,
        edges: getEdges.value
      }
      const tempVersionId = await historyStore.createTempVersion(currentStrategyId.value, flowData)
      currentVersionId.value = tempVersionId
      hasUnsavedChanges.value = true
      console.info(`[ensureVersionId] 已创建临时版本：${tempVersionId}`)
      message.info('已创建临时版本用于保存回测结果')
      return tempVersionId
    }

    // 如果没有策略，创建临时策略和临时版本
    const tempStrategyName = `临时策略_${new Date().toLocaleTimeString()}`
    const tempStrategyId = await historyStore.addStrategy(tempStrategyName)
    currentStrategyId.value = tempStrategyId

    const flowData = {
      nodes: getNodes.value,
      edges: getEdges.value
    }
    const tempVersionId = await historyStore.createTempVersion(tempStrategyId, flowData)
    currentVersionId.value = tempVersionId
    hasUnsavedChanges.value = true
    console.info(`[ensureVersionId] 已创建临时策略：${tempStrategyId}, 临时版本：${tempVersionId}`)
    message.info('已创建临时策略和版本用于保存回测结果')
    return tempVersionId
  }

  /**
   * 从历史版本加载流程图
   */
  const loadVersionFromHistory = async (version, message, reportViewRef) => {
    try {
      let versionData = version
      // 如果传入的是ID，则从 store 中查找对应的版本对象
      if (typeof version === 'string' || typeof version === 'number') {
        versionData = versions.value.find(v => v.id == version)
        if (!versionData) {
          message.error('未找到版本数据')
          return
        }
      }
      // 加载流程图数据（flowData 单独存储在 IndexedDB 中）
      const flowData = await historyStore.loadVersionFlowData(versionData.id)

      // 如果没有找到 flowData，尝试使用 version 中存储的 flowData（如果有）
      if (!flowData && versionData.flowData) {
        console.info(`版本 ${versionData.id} 的 flowData 来自内存缓存`)
      }

      if (!flowData && !versionData.flowData) {
        message.error('版本数据中没有流程图信息')
        return
      }

      // 使用 flowData（优先使用 IndexedDB 中的，其次使用内存中的）
      const finalFlowData = flowData || versionData.flowData

      // 将 flowData 附加到 versionData 上，供后续使用
      versionData.flowData = finalFlowData

      // 清空当前画布
      removeNodes(getNodes.value.map(n => n.id))
      removeEdges(getEdges.value.map(e => e.id))

      const loadedNodes = finalFlowData.nodes || []
      const loadedEdges = finalFlowData.edges || []

      // 添加节点
      if (loadedNodes.length > 0) {
        await nextTick(() => {
          addNodes(loadedNodes.map(node => ({
            ...node,
            type: node.type || 'custom'
          })))
        })

        // 更新节点计数器（假设节点ID为数字字符串）
        const maxId = Math.max(...loadedNodes.map(node => parseInt(node.id) || 0))
        nodeIdCounter.value = maxId + 1
      }

      // 添加边（需确保节点已添加）
      nextTick(() => {
        const validEdges = loadedEdges.filter(edge => {
          const sourceExists = getNodes.value.some(n => n.id === edge.source)
          const targetExists = getNodes.value.some(n => n.id === edge.target)
          if (!sourceExists || !targetExists) {
            console.warn(`边 ${edge.id} 引用了不存在的节点: source=${edge.source}, target=${edge.target}`)
            return false
          }
          return true
        })
        const curEdges = validEdges.map(edge => ({
          ...edge,
          source: String(edge.source),
          target: String(edge.target),
          type: edge.type || 'default',
          markerEnd: edge.markerEnd || {
            type: MarkerType.ArrowClosed,
            color: 'var(--primary)',
          },
          style: edge.style || {
            stroke: 'var(--primary)',
            strokeWidth: 2,
          }
        }))
        addEdges(curEdges)

        message.success(`已加载版本: ${versionData.name || versionData.id}`)

        // 更新当前策略和版本ID
        currentStrategyId.value = versionData.strategyId || null
        currentVersionId.value = versionData.id
        hasUnsavedChanges.value = false

        // 适应视图
        setTimeout(() => {
          fitView({ padding: 0.25 })
        }, 100)

        // 加载该版本的回测结果（如果有）
        // 从流程图中提取回测周期和标的代码
        let backtestStartDate
        let backtestEndDate
        let backtestSymbol

        if (finalFlowData?.nodes) {
          // 从信号节点提取回测周期
          const signalNode = finalFlowData.nodes.find((n) =>
            n.data?.params?.['回测周期']?.value
          )
          if (signalNode) {
            const rangeDate = signalNode.data.params['回测周期'].value
            if (rangeDate && rangeDate.length === 2) {
              backtestStartDate = rangeDate[0]
              backtestEndDate = rangeDate[1]
              console.info(`[loadVersionFromHistory] 提取回测周期：${backtestStartDate} - ${backtestEndDate}`)
            }
          }

          // 从输入节点（如 QuoteNode）提取标的代码
          const inputNode = finalFlowData.nodes.find((n) =>
            n.data?.params?.['代码']?.value
          )
          if (inputNode) {
            const codes = inputNode.data.params['代码'].value
            const symbols = codes.split(',').map((s) => s.trim()).filter((s) => s.length > 0)
            if (symbols.length > 0) {
              backtestSymbol = symbols.join(',')  // 传递所有标的代码，逗号分隔
              console.info(`[loadVersionFromHistory] 提取标的代码：${backtestSymbol}`)
            }
          }
        }

        // 调用 ReportView 的 loadBacktestResultFromVersion 加载回测结果和历史价格数据
        if (reportViewRef?.value && reportViewRef.value.loadBacktestResultFromVersion) {
          reportViewRef.value.loadBacktestResultFromVersion(
            versionData.id,
            backtestStartDate,
            backtestEndDate,
            backtestSymbol
          )
        } else {
          console.warn('[loadVersionFromHistory] ReportView 未找到 loadBacktestResultFromVersion 方法')
        }
      })
    } catch (error) {
      console.error('加载版本数据失败:', error)
      message.error('加载版本数据失败')
    }
  }

  return {
    saveFlow,
    saveAsNewVersion,
    createNewVersion,
    loadSavedFlow,
    loadVersionFromHistory,
    validateFlow,
    ensureVersionId,
    formatDateForRemark,
    // 暴露 store 供外部使用
    historyStore,
    strategies,
    versions
  }
}
