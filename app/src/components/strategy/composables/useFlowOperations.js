import { nextTick } from 'vue'
import { MarkerType } from '@vue-flow/core'
import { createStrategyNode } from '@/lib/nodes'
import { updateAllSuccessorDebugNodes } from '@/lib/nodes/useDebugNodeFields'
import { functionInputSlots } from '@/lib/nodes/configs/function'
import { breakoutInputSlots } from '@/lib/nodes/configs/breakout'

/**
 * 流程图操作 composable
 * 处理节点和边的 CRUD 操作、选择、拖拽、连接等
 */
export function useFlowOperations(state) {
  const {
    addNodes,
    addEdges,
    screenToFlowCoordinate,
    updateNode,
    getNodes,
    getEdges,
    removeNodes,
    removeEdges,
    getConnectedEdges,
    getSelectedNodes,
    getSelectedEdges,
    addSelectedNodes,
    addSelectedEdges,
    removeSelectedEdges,
    removeSelectedNodes,
    fitView,
    selectedNodes,
    selectedEdges,
    nodeIdCounter,
    contextMenu
  } = state

  /**
   * 选择节点
   */
  const selectNode = (node) => {
    if (!selectedNodes.value.find(n => n.id === node.id)) {
      addSelectedNodes([node])
    }
  }

  /**
   * 取消选择节点
   */
  const deselectNode = (node) => {
    const index = selectedNodes.value.findIndex(n => n.id === node.id)
    if (index > -1) {
      removeSelectedNodes([selectedNodes.value[index]])
      selectedNodes.value.splice(index, 1)
    }
  }

  /**
   * 切换节点选择状态
   */
  const toggleNodeSelection = (node) => {
    if (selectedNodes.value.find(n => n.id === node.id)) {
      deselectNode(node)
    } else {
      selectNode(node)
    }
  }

  /**
   * 选择边
   */
  const selectEdge = (edge) => {
    if (!selectedEdges.value.find(e => e.id === edge.id)) {
      addSelectedEdges([edge])
    }
  }

  /**
   * 取消选择边
   */
  const deselectEdge = (edge) => {
    removeSelectedEdges([edge])
  }

  /**
   * 切换边选择状态
   */
  const toggleEdgeSelection = (edge) => {
    if (selectedEdges.value.find(e => e.id === edge.id)) {
      deselectEdge(edge)
    } else {
      selectEdge(edge)
    }
  }

  /**
   * 清空所有选择
   */
  const clearSelection = () => {
    removeSelectedNodes(selectedNodes.value)
    removeSelectedEdges(selectedEdges.value)
  }

  /**
   * 清空边选择
   */
  const clearEdgeSelection = () => {
    removeSelectedEdges(selectedEdges.value)
  }

  /**
   * 删除选中的节点
   */
  const deleteSelectedNodes = () => {
    if (selectedNodes.value.length === 0) return

    // 获取所有选中节点的ID
    const nodeIdsToDelete = selectedNodes.value.map(node => node.id)

    // 获取与这些节点相关的所有边
    const edgesToRemove = []
    nodeIdsToDelete.forEach(nodeId => {
      const connectedEdges = getConnectedEdges(nodeId)
      edgesToRemove.push(...connectedEdges)
    })

    // 删除边
    removeEdges(edgesToRemove.map(edge => edge.id))

    // 删除节点
    removeNodes(nodeIdsToDelete)

    // 清空选择
    clearSelection()

    // 关闭菜单
    if (contextMenu) {
      contextMenu.value.visible = false
    }
  }

  /**
   * 删除选中的边（或指定边列表）
   */
  const deleteSelectedEdges = (edgesToDelete = null) => {
    const edges = edgesToDelete || selectedEdges.value
    if (edges.length === 0) return

    const edgeIdsToDelete = edges.map(edge => edge.id)
    
    // 收集被删除边涉及的 EMD 源节点
    const emdSourceNodes = new Set()
    edges.forEach(edge => {
      if (edge.sourceHandle === 'energy_velocity' || edge.sourceHandle === 'volume_regime') {
        emdSourceNodes.add(edge.source)
      }
    })

    removeEdges(edgeIdsToDelete)

    // 清空选择（仅当使用默认选中边时）
    if (!edgesToDelete) {
      clearEdgeSelection()
    }

    // 边删除后，更新相关 EMD 节点的 compute 参数
    nextTick(() => {
      emdSourceNodes.forEach(nodeId => {
        updateEmdComputeFlags(nodeId, getNodes.value, getEdges.value)
      })
    })
  }

  /**
   * 复制选中的节点（含内部连接）
   */
  const duplicateSelectedNodes = () => {
    if (selectedNodes.value.length === 0) return

    const duplicatedNodes = []
    const idMapping = new Map()  // 旧ID → 新ID 映射

    // 1. 复制节点（生成新ID，偏移位置）
    selectedNodes.value.forEach(node => {
      const newId = `${nodeIdCounter.value++}`
      idMapping.set(node.id, newId)

      duplicatedNodes.push({
        ...node,
        id: newId,
        position: {
          x: node.position.x + 30,  // 偏移避免重叠
          y: node.position.y + 30
        },
        // 深拷贝 params
        data: {
          ...node.data,
          params: JSON.parse(JSON.stringify(node.data.params))
        }
      })
    })

    // 2. 复制选中节点之间的内部连接
    const oldSelectedIds = new Set(selectedNodes.value.map(n => n.id))
    const newEdges = []

    selectedNodes.value.forEach(node => {
      const connectedEdges = getConnectedEdges(node.id)
      connectedEdges.forEach(edge => {
        // 只有当边的两端都在选中节点内时才复制
        if (oldSelectedIds.has(edge.source) && oldSelectedIds.has(edge.target)) {
          const newSourceId = idMapping.get(edge.source)
          const newTargetId = idMapping.get(edge.target)
          // 避免重复添加
          if (newSourceId && newTargetId) {
            const newEdgeId = `e${newSourceId}-${edge.sourceHandle || ''}-${newTargetId}-${edge.targetHandle || ''}`
            // 检查是否已存在
            const exists = newEdges.some(e => e.id === newEdgeId)
            if (!exists) {
              newEdges.push({
                id: newEdgeId,
                source: newSourceId,
                target: newTargetId,
                sourceHandle: edge.sourceHandle,
                targetHandle: edge.targetHandle,
                type: 'default',
                markerEnd: edge.markerEnd || {
                  type: MarkerType.ArrowClosed,
                  color: 'var(--primary)',
                },
                style: edge.style || {
                  stroke: 'var(--primary)',
                  strokeWidth: 2,
                }
              })
            }
          }
        }
      })
    })

    // 3. 添加新节点到画布
    addNodes(duplicatedNodes)

    // 4. 添加新边
    if (newEdges.length > 0) {
      addEdges(newEdges)
    }

    // 5. 选中新复制的节点
    nextTick(() => {
      const newNodes = duplicatedNodes
        .map(n => getNodes.value.find(node => node.id === n.id))
        .filter(Boolean)
      addSelectedNodes(newNodes)
    })
  }

  /**
   * 节点右键菜单事件
   */
  const onNodeContextMenu = (event) => {
    event.event.preventDefault()

    const { node, event: mouseEvent } = event

    // 如果右键点击的节点不在选中列表中，则先选中它
    if (!selectedNodes.value.find(n => n.id === node.id)) {
      clearSelection()
      selectNode(node)
    }

    contextMenu.value = {
      visible: true,
      x: mouseEvent.clientX,
      y: mouseEvent.clientY,
      targetNode: node
    }
  }

  /**
   * 节点点击事件
   */
  const onNodeClick = ({ node, event }) => {
    // 如果按住了 Ctrl 或 Cmd 键，则切换选择状态
    if (event.ctrlKey || event.metaKey) {
      toggleNodeSelection(node)
    } else {
      // 如果没有按修饰键，则清空选择并选择当前节点
      clearSelection()
      selectNode(node)
    }
  }

  /**
   * 画布点击事件（点击空白处清空选择）
   */
  const onPaneClick = () => {
    clearSelection()
  }

  /**
   * 边点击事件
   */
  const onEdgeClick = (event) => {
    const { edge, event: mouseEvent } = event
    mouseEvent.stopPropagation()

    // 如果按住了 Ctrl 或 Cmd 键，则切换选择状态
    if (mouseEvent.ctrlKey || mouseEvent.metaKey) {
      toggleEdgeSelection(edge)
    } else {
      // 如果没有按修饰键，则清空选择并选择当前边
      clearSelection()
      selectEdge(edge)
    }
  }

  /**
   * 画布右键菜单事件
   */
  const onSelectionContextMenu = (event) => {
    event.preventDefault()
    contextMenu.value = {
      visible: true,
      x: event.event.clientX,
      y: event.event.clientY,
      targetNode: null
    }
  }

  /**
   * 拖拽放置处理
   */
  const onDrop = (event) => {
    const { dataTransfer, clientX, clientY } = event
    const nodeId = dataTransfer?.getData('application/vueflow')
    if (!nodeId) return

    const position = screenToFlowCoordinate({
      x: clientX,
      y: clientY,
    })

    try {
      const newNode = createStrategyNode({
        id: nodeIdCounter.value,
        registryId: nodeId,
        position,
      })
      nodeIdCounter.value++
      addNodes([newNode])
    } catch (error) {
      console.warn(`[Flow] ${error.message}`)
    }
  }

  /**
   * 拖拽悬停处理
   */
  const onDragOver = (event) => {
    event.preventDefault()
    if (event.dataTransfer) {
      event.dataTransfer.dropEffect = 'move'
    }
  }

  /**
   * 画布准备好后适应视图
   */
  const onPaneReady = () => {
    setTimeout(() => {
      fitView({ padding: 0.25 })
    }, 100)
  }

  /**
   * 更新节点数据
   */
  const updateNodeData = (nodeId, paramKey, newValue) => {
    const nodeIndex = getNodes.value.findIndex(node => node.id === nodeId)
    if (nodeIndex !== -1) {
      // 创建新的节点对象以触发响应式更新
      const updatedNode = {
        ...getNodes.value[nodeIndex],
        data: {
          ...getNodes.value[nodeIndex].data,
          params: {
            ...getNodes.value[nodeIndex].data.params,
            [paramKey]: {
              ...getNodes.value[nodeIndex].data.params[paramKey],
              value: newValue
            }
          }
        }
      }

      // 更新节点
      getNodes.value[nodeIndex] = updatedNode

      // 如果参数变化影响其他参数的可见性，可以在这里处理
      if (paramKey === '缺失值' && newValue === '填充') {
        // 显示填充值参数
        getNodes.value[nodeIndex].data.params.填充值.visible = true
      } else if (paramKey === '缺失值' && newValue !== '填充') {
        // 隐藏填充值参数
        getNodes.value[nodeIndex].data.params.填充值.visible = false
      } else if (paramKey === 'label') {
        const node = getNodes.value.find(n => n.id === nodeId)
        if (node) {
          // 更新 node 的 title
          node.data.label = newValue
        }
      }
    }
  }

  /**
   * 更新 EMD 节点的 compute 参数（根据 handle 连接状态）
   */
  const updateEmdComputeFlags = (nodeId, nodes, edges) => {
    const node = nodes.find(n => n.id === nodeId)
    if (!node || node.data?.nodeType !== 'emd') return

    const nodeEdges = edges.filter(e => e.source === nodeId)
    const connectedHandles = new Set(nodeEdges.map(e => e.sourceHandle))

    const hasEnergyVelocity = connectedHandles.has('energy_velocity')
    const hasVolumeRegime = connectedHandles.has('volume_regime')

    // 只有值变化时才更新（key 是中文 label）
    const currentEV = node.data.params?.['能量变化率']?.value === '能量变化率'
    const currentVR = node.data.params?.['成交量体制']?.value === '成交量体制'

    if (currentEV !== hasEnergyVelocity || currentVR !== hasVolumeRegime) {
      updateNodeData(nodeId, {
        '能量变化率': { value: hasEnergyVelocity ? '能量变化率' : '' },
        '成交量体制': { value: hasVolumeRegime ? '成交量体制' : '' }
      })
    }
  }

  /**
   * 连接创建事件处理
   */
  const onConnect = (connection) => {
    const newEdge = {
      id: `e${connection.source}-${connection.sourceHandle}-${connection.target}-${connection.targetHandle}`,
      source: connection.source,
      target: connection.target,
      sourceHandle: connection.sourceHandle,
      targetHandle: connection.targetHandle,
      type: 'default',
      markerEnd: {
        type: MarkerType.ArrowClosed,
        color: 'var(--primary)',
      },
      style: {
        stroke: 'var(--primary)',
        strokeWidth: 2,
      },
    }
    // 添加到边数组
    addEdges([newEdge])

    // 连接产生后，更新所有后继 DebugNode 的字段选项
    nextTick(() => {
      updateAllSuccessorDebugNodes(connection.target, getNodes.value, getEdges.value)
      // 更新 EMD 节点的 compute 参数
      updateEmdComputeFlags(connection.source, getNodes.value, getEdges.value)
    })
  }

  /**
   * 设置 EMD 出边的 IMF 编号（用于节点底部 EdgeImfSelector）
   *
   * newIndex: null = 未选择（所有 IMF 都给下游）/ 0~N-1 = 具体 IMF 编号
   * 通过 remove + addEdges 重写 edge.id 触发 Vue Flow 重渲染
   */
  const setEdgeImfIndex = (edgeId, newIndex) => {
    const edge = getEdges.value.find(e => e.id === edgeId)
    if (!edge) return null

    const normalized = (newIndex === null || newIndex === undefined) ? null : Number(newIndex)
    if (normalized !== null && (!Number.isInteger(normalized) || normalized < 0)) return null

    const current = (edge.data?.imfIndex === undefined) ? null : edge.data.imfIndex
    if (current === normalized) return edge

    const newData = { ...(edge.data || {}), imfIndex: normalized }
    const imfPart = normalized !== null ? `-IMF_${normalized}` : ''
    const newId = `e${edge.source}${imfPart}-${edge.target}-${edge.targetHandle}`
    const newEdge = { ...edge, id: newId, data: newData }

    removeEdges([edge])
    addEdges([newEdge])
    nextTick(() => {
      const updated = getEdges.value.find(e => e.id === newId)
      if (updated) addSelectedEdges([updated])
    })
    return newEdge
  }

  /**
   * 连接验证
   */
  const isValidConnection = (connection) => {
    // 防止连接到自身
    if (connection.source === connection.target) {
      return false
    }

    // 验证 source：按源节点类型判断 sourceHandle 合法性
    const sourceNode = getNodes.value.find(n => n.id === connection.source)
    if (!sourceNode) return false

    const sourceNodeType = sourceNode.data?.nodeType
    const sourceHandle = connection.sourceHandle

    let isValidSource = false
    if (sourceNodeType === 'input') {
      isValidSource = sourceHandle?.startsWith('field-')
    } else if (sourceNodeType === 'emd' || sourceNodeType === 'hmm') {
      isValidSource = !!sourceHandle
    } else {
      isValidSource = sourceHandle === 'output'
    }

    if (!isValidSource) {
      return false
    }

    // 获取目标节点信息
    const targetNode = getNodes.value.find(n => n.id === connection.target)
    if (!targetNode) return false

    const targetNodeType = targetNode.data?.nodeType
    const targetHandle = connection.targetHandle

    // FunctionNode 命名槽位验证
    if (targetNodeType === 'function') {
      // 目标 handle 必须是 'input-{slot}' 格式
      if (!targetHandle || !targetHandle.startsWith('input-')) {
        return false
      }

      const slotName = targetHandle.replace('input-', '')
      const method = targetNode.data.params?.method?.value || 'MA'

      // 从 functionInputSlots 获取该方法的槽位定义
      const slots = functionInputSlots[method] || []
      const targetSlot = slots.find(s => s.slot === slotName)

      if (!targetSlot) return false

      // 如果源是 InputNode 的 field handle，验证 field 匹配
      if (connection.sourceHandle?.startsWith('field-')) {
        const sourceField = connection.sourceHandle.replace('field-', '')
        if (sourceField !== targetSlot.field) return false
      }

      // 防止重复连接同一槽位
      const existingConnection = getEdges.value.find(edge =>
        edge.target === connection.target &&
        edge.targetHandle === connection.targetHandle
      )
      return !existingConnection
    }

    // BreakoutNode 命名槽位验证
    if (targetNodeType === 'breakout') {
      if (!targetHandle || !targetHandle.startsWith('input-')) {
        return false
      }

      const slotName = targetHandle.replace('input-', '')
      const targetSlot = breakoutInputSlots.find(s => s.slot === slotName)
      if (!targetSlot) return false

      // 防止重复连接同一槽位
      const existingConnection = getEdges.value.find(edge =>
        edge.target === connection.target &&
        edge.targetHandle === connection.targetHandle
      )
      return !existingConnection
    }

    // 普通节点验证
    if (targetHandle !== 'input') {
      return false
    }

    // 防止重复连接
    const existingConnection = getEdges.value.find(edge =>
      edge.source === connection.source &&
      edge.target === connection.target &&
      edge.sourceHandle === connection.sourceHandle &&
      edge.targetHandle === connection.targetHandle
    )

    return !existingConnection
  }

  return {
    // 节点操作
    selectNode,
    deselectNode,
    toggleNodeSelection,
    // 边操作
    selectEdge,
    deselectEdge,
    toggleEdgeSelection,
    clearEdgeSelection,
    // 通用操作
    clearSelection,
    deleteSelectedNodes,
    deleteSelectedEdges,
    duplicateSelectedNodes,
    // 事件处理
    onNodeContextMenu,
    onNodeClick,
    onPaneClick,
    onEdgeClick,
    onSelectionContextMenu,
    onDrop,
    onDragOver,
    onPaneReady,
    updateNodeData,
    onConnect,
    setEdgeImfIndex,
    isValidConnection
  }
}
