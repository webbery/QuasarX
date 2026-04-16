import { nextTick } from 'vue'
import { MarkerType } from '@vue-flow/core'
import { nodeTypeConfigs } from '../../flow/nodeConfigs'

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
   * 删除选中的边
   */
  const deleteSelectedEdges = () => {
    if (selectedEdges.value.length === 0) return

    const edgeIdsToDelete = selectedEdges.value.map(edge => edge.id)
    removeEdges(edgeIdsToDelete)

    // 清空选择
    clearEdgeSelection()
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
    const nodeType = dataTransfer?.getData('application/vueflow')
    
    if (nodeType && nodeTypeConfigs[nodeType]) {
      const position = screenToFlowCoordinate({
        x: clientX,
        y: clientY,
      })

      const newNode = {
        id: `${nodeIdCounter.value++}`,
        type: 'custom',
        position,
        data: {
          label: nodeTypeConfigs[nodeType].label,
          nodeType: nodeTypeConfigs[nodeType].nodeType,
          params: JSON.parse(JSON.stringify(nodeTypeConfigs[nodeType].params)) // 深拷贝参数
        }
      }

      addNodes([newNode])
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
  }

  /**
   * 连接验证
   */
  const isValidConnection = (connection) => {
    // 防止连接到自身
    if (connection.source === connection.target) {
      return false
    }

    // 验证连接方向：起点(source)只能连接到终点(target)
    // 普通节点: sourceHandle='output' -> targetHandle='input'
    // 数据输入节点: sourceHandle='field-close'/'field-open'/'field-high'/'field-low'/'field-volume' -> targetHandle='input'
    const dataFieldHandles = ['field-close', 'field-open', 'field-high', 'field-low', 'field-volume']
    const isValidSource = connection.sourceHandle === 'output' || dataFieldHandles.includes(connection.sourceHandle)
    
    if (!isValidSource || connection.targetHandle !== 'input') {
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
    isValidConnection
  }
}
