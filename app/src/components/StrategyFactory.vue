<template>
    <div class="main-container">
      <div class="tab-header">
        <!-- 选项卡切换 -->
        <div class="tabs">
            <button 
                class="tab-button" 
                :class="{ active: activeTab === 'flow' }"
                @click="activeTab = 'flow'"
            >
                <i class="fas fa-project-diagram"></i>
                流程图
            </button>
            <button 
                class="tab-button" 
                :class="{ active: activeTab === 'backtest' }"
                @click="activeTab = 'backtest'"
            >
                <i class="fas fa-chart-line"></i>
                回测结果
            </button>
        </div>
      </div>
      <!-- 流程图面板 -->
      <div v-show="activeTab === 'flow'" class="flow-panel">
        <div class="flow-container-wrapper">
          <!-- 左下角信息面板 -->
          <InfoPanel :messages="infoMessages" />

          <div class="flow-container">
              <VueFlow
                  :nodes="nodes"
                  :edges="edges"
                  @pane-ready="onPaneReady"
                  @drop="onDrop"
                  @dragover="onDragOver"
                  @node-context-menu="onNodeContextMenu"
                  @edge-click="onEdgeClick"
                  @selection-drag-start="onSelectionDragStart"
                  @selection-drag="onSelectionDrag"
                  @selection-drag-stop="onSelectionDragStop"
                  @selection-context-menu="onSelectionContextMenu"
                  @pane-click="onPaneClick"
                  @connect="onConnect"
                  @edges-delete="onEdgesDelete"
                  :is-valid-connection="isValidConnection"
              >
                  <template #node-custom="nodeProps">
                      <FlowNode :node="nodeProps" 
                          @update-node="updateNodeData"
                          @node-click="onNodeClick"
                          @node-context-menu="onNodeContextMenu"
                      />
                  </template>
                  
                  <!-- 自定义连接线样式 -->
                  <template #connection-line="connectionProps">
                      <FlowConnectLine v-bind="connectionProps" />
                  </template>
              </VueFlow>
          </div>
          <!-- 右下角功能按钮 -->
          <div class="flow-actions">
            <button class="action-btn" @click="saveFlow" title="保存策略图">
                <i class="fas fa-save"></i>
                保存
            </button>
            <button class="action-btn" @click="saveAsNewVersion" title="另存为新版本">
                <i class="fas fa-save"></i>
                另存为
            </button>
            <button class="action-btn" @click="showHistoryStrategy" title="策略面板">
                <i class="fas fa-redo"></i>
                策略面板
            </button>
            <button class="action-btn" @click="showStrategyNodes" title="节点面板">
                <i class="fas fa-play"></i>
                节点面板
            </button>
          </div>
        </div>
      </div>

      <!-- 回测结果面板 -->
      <div v-show="activeTab === 'backtest'" class="backtest-panel">
          <!-- 报表区域 -->
          <ReportView ref="reportViewRef"></ReportView>
      </div>

      <!-- 右键菜单 -->
      <div 
          v-if="contextMenu.visible" 
          class="context-menu" 
          :style="{ left: contextMenu.x + 'px', top: contextMenu.y + 'px' }"
          @click.stop
      >
          <div class="context-menu-item" @click="deleteSelectedNodes">
              <i class="fas fa-trash"></i>
              删除节点
          </div>
          <div class="context-menu-item" @click="duplicateSelectedNodes">
              <i class="fas fa-copy"></i>
              复制节点
          </div>
          <div class="context-menu-divider"></div>
          <div class="context-menu-item" @click="clearSelection">
              <i class="fas fa-times"></i>
              取消选择
          </div>
    </div>
    <PromptDialog ref="promptDialogRef" />
  </div>
</template>

<script setup>
import { ref, onMounted, computed, onUnmounted, provide, watch, nextTick, defineEmits } from 'vue'
import { useVueFlow, VueFlow, MarkerType } from '@vue-flow/core'
import { message } from '@/tool'
import FlowNode from './flow/FlowNode.vue'
import FlowConnectLine from './flow/FlowConnectLine.vue'
import ReportView from './ReportView.vue'
import InfoPanel from './InfoPanel.vue'
import PromptDialog from './PromptDialog.vue'
import axios from 'axios'
import sseService from '@/ts/SSEService';
import { useHistoryStore } from '@/stores/history'
import { usePortfolioStore } from '@/stores/portfolio'
import { storeToRefs } from 'pinia'
import { keyMap, nodeTypeConfigs } from './flow/nodeConfigs'

// 初始化 portfolio store
const portfolioStore = usePortfolioStore()

const {
    fitView, 
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
    edgesSelectionActive,
    nodesSelectionActive,
    removeSelectedNodes,
    onNodesInitialized } = useVueFlow()
const activeTab = ref('flow')
const selectedNodes = ref([])
const selectedEdges = ref([])
const reportViewRef = ref(null)
const emit = defineEmits(['show-history', 'show-flow-components', 'load-version'])

// Pinia Store
const historyStore = useHistoryStore()
const { strategies, versions } = storeToRefs(historyStore)

// localStorage 键名
const FLOW_STORAGE_KEY = 'vue-flow-saved-strategy'
const LAST_BACKTEST_RESULT = 'last_backtest_result'
let nodeIdCounter = 10

const promptDialogRef = ref()
// 当前选中的策略和版本（用于判断保存行为）
const currentStrategyId = ref(null)
const currentVersionId = ref(null)
const strategyNameInput = ref('')
const showNewStrategyDialog = ref(false)
// 未保存更改标记（用于后续提示）
const hasUnsavedChanges = ref(false)
// 信息面板消息
const infoMessages = ref([])
// 回测进度状态（已移除，进度作为消息处理）

// 右键菜单状态
const contextMenu = ref({
    visible: false,
    x: 0,
    y: 0,
    targetNode: null
})


// 监听点击事件以关闭菜单
const closeContextMenu = () => {
    contextMenu.value.visible = false
}

// 添加键盘事件监听
const onKeyDown = (event) => {
  // 按 Delete 或 Backspace 键删除选中的元素
  if ((event.key === 'Delete' || event.key === 'Backspace') &&
      (selectedNodes.value.length > 0 || selectedEdges.value.length > 0)) {
    event.preventDefault()

    if (selectedNodes.value.length > 0) {
      deleteSelectedNodes()
    } else if (selectedEdges.value.length > 0) {
      deleteSelectedEdges()
    }
  }
}

// 添加信息到面板
const addInfoMessage = (text, type = 'info') => {
  const timestamp = new Date()
  infoMessages.value.push({ text, type, timestamp })
  // 限制消息数量
  if (infoMessages.value.length > 100) {
    infoMessages.value = infoMessages.value.slice(-100)
  }
}

// 更新或创建进度消息
const updateProgressMessage = (backtestId, strategy, progress, message) => {
  const existingProgress = infoMessages.value.find(
    m => m.type === 'progress' && m.backtestId === backtestId
  )
  if (existingProgress) {
    existingProgress.progress = progress
    existingProgress.message = message
  } else {
    infoMessages.value.push({
      type: 'progress',
      backtestId,
      strategy,
      progress,
      message,
      timestamp: new Date()
    })
  }
  if (infoMessages.value.length > 100) {
    infoMessages.value = infoMessages.value.slice(-100)
  }
}

// 清空消息
const clearMessages = () => {
  infoMessages.value = []
}

// 添加全局点击事件监听
onMounted(async () => {
    // 初始化 IndexedDB 数据
    await historyStore.initialize()
    document.addEventListener('click', closeContextMenu)
    document.addEventListener('keydown', onKeyDown)
    // loadSavedFlow()
    sseService.on('strategy', onStrategyMessageUpdate)
    sseService.on('backtest_progress', onBacktestProgressUpdate)
})

onUnmounted(() => {
    document.removeEventListener('click', closeContextMenu)
    document.removeEventListener('keydown', onKeyDown)
    sseService.off('strategy')
    sseService.off('backtest_progress')
})

// 提供 selectedNodes 给子组件
provide('selectedNodes', selectedNodes)
// 提供 selectedEdges 给子组件
provide('selectedEdges', selectedEdges)
// 提供 portfolioConfigs 给 FlowNode 使用
provide('portfolioConfigs', computed(() => portfolioStore.portfolioConfigs))

// 监听 Vue Flow 的选中状态变化
watch(getSelectedNodes, (newSelectedNodes) => {
    // 同步 Vue Flow 的选中状态到我们的 selectedNodes
    selectedNodes.value = newSelectedNodes
}, { deep: true })

// 监听选中的边变化
watch(getSelectedEdges, (newSelectedEdges) => {
  selectedEdges.value = newSelectedEdges
}, { deep: true })

// 监听节点和边变化，标记未保存
watch(() => getNodes.value, () => {
  hasUnsavedChanges.value = true
}, { deep: true, immediate: false })

watch(() => getEdges.value, () => {
  hasUnsavedChanges.value = true
}, { deep: true, immediate: false })

// 监听选项卡切换，当切换到回测结果时更新价格图表
watch(activeTab, async (newTab) => {
  if (newTab === 'backtest' && reportViewRef.value) {
    console.info('[StrategyFactory] 切换到回测结果选项卡')

    // 等待 ReportView 组件完成初始化
    await nextTick()
    await new Promise(resolve => setTimeout(resolve, 300))

    // 从信号节点获取标的代码和日期范围
    const signalNode = getNodes.value.find(node => node.data.nodeType === 'signal')
    const executionNode = getNodes.value.find(node => node.data.nodeType === 'execution')
    if (signalNode && executionNode) {
      const codes = signalNode.data.params['代码']?.value
      const rangeDate = executionNode.data.params['回测周期']?.value

      if (codes && rangeDate && rangeDate.length === 2) {
        const symbols = codes.split(',').map(s => s.trim()).filter(s => s.length > 0)
        if (symbols.length > 0 && reportViewRef.value.updatePrice) {
          console.info(`[StrategyFactory] 更新价格图表：${symbols[0]}, ${rangeDate[0]} - ${rangeDate[1]}`)
          reportViewRef.value.updatePrice(symbols[0], rangeDate[0], rangeDate[1])
        }
      }
    } else {
      console.warn('[StrategyFactory] 未找到信号节点，无法更新价格图表')
    }
  }
})

const validNodes = computed({
    get: () => getNodes.value,
    set: (newNodes) => {
        // 这里可以处理节点更新
    }
})

// 节点右键菜单事件
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

// 节点点击事件
const onNodeClick = ({ node, event }) => {
    // 如果按住了 Ctrl 或 Cmd 键，则切换选择状态
    console.info('click node')
    if (event.ctrlKey || event.metaKey) {
        toggleNodeSelection(node)
    } else {
        // 如果没有按修饰键，则清空选择并选择当前节点
        clearSelection()
        selectNode(node)
    }
}

// 画布点击事件（点击空白处清空选择）
const onPaneClick = () => {
    clearSelection()
}

// 选择节点
const selectNode = (node) => {
    if (!selectedNodes.value.find(n => n.id === node.id)) {
        console.info('add selected node', node)
        addSelectedNodes([node])
    }
}

// 取消选择节点
const deselectNode = (node) => {
    const index = selectedNodes.value.findIndex(n => n.id === node.id)
    if (index > -1) {
      removeSelectedNodes(selectedNodes.value[index])
      selectedNodes.value.splice(index, 1)
    }
}

// 切换节点选择状态
const toggleNodeSelection = (node) => {
    if (selectedNodes.value.find(n => n.id === node.id)) {
        deselectNode(node)
    } else {
        selectNode(node)
    }
}

// 清空选择
const clearSelection = () => {
    removeSelectedNodes(selectedNodes.value)
}

// 选择拖动相关事件
// const onSelectionDragStart = () => {
//     console.log('开始拖动选择')
// }

// const onSelectionDrag = () => {
//     console.log('拖动选择中')
// }

// const onSelectionDragStop = () => {
//     console.log('停止拖动选择')
// }

// 右键菜单
const onSelectionContextMenu = (event) => {
    event.preventDefault()
    contextMenu.value = {
        visible: true,
        x: event.event.clientX,
        y: event.event.clientY,
        targetNode: null
    }
}

// 删除选中的节点
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
    contextMenu.value.visible = false
}

// 连接创建事件处理
const onConnect = (connection) => {
  console.log('创建连接:', connection)
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

  // 检查是否是信号节点连接到数据输入节点，如果是，自动同步 code
  // syncCodeFromQuoteInput(connection.target, connection.source)
}

// const onConnectStart = (event) => {
//   console.log('=== 连接开始 ===')
//   console.log('连接开始事件:', event)
// }

// const onConnectEnd = (event) => {
//   console.log('=== 连接结束 ===')
//   console.log('连接结束事件:', event)
// }

// const onConnectAbort = (event) => {
//   console.log('=== 连接中止 ===')
//   console.log('连接中止事件:', event)
// }

// 连接验证（可选）
const isValidConnection = (connection) => {
  // 防止连接到自身
  // console.info('isValidConnection:', connection)
  if (connection.source === connection.target) {
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

function onStrategyMessageUpdate(message) {
  console.info('onStrategyMessageUpdate message:', message)
  const data = message.data
  addInfoMessage(data.message, data.type)
}

// 回测进度更新处理
function onBacktestProgressUpdate(message) {
  const data = message.data
  const backtestId = data.strategy + '_' + data.start_time
  updateProgressMessage(backtestId, data.strategy, data.progress || 0, data.message)
}

// 替换对象键名的辅助函数
function replaceKeysInObject(obj, keyMapping) {
  Object.keys(obj).forEach(key => {
    // 如果当前键在映射中存在，则进行替换
    if (keyMapping[key]) {
      const newKey = keyMapping[key];
      // 只有当新键名与旧键名不同时才进行替换
      if (newKey !== key) {
        obj[newKey] = obj[key];
        delete obj[key];
      }
    }
    
    // 递归处理嵌套对象
    if (typeof obj[key] === 'object' && obj[key] !== null && !Array.isArray(obj[key])) {
      replaceKeysInObject(obj[key], keyMapping);
    }
  });
}

const toServerKey = (json_data) => {
  let reverseKeyMap = Object.fromEntries(
    Object.entries(keyMap).map(([key, value]) => [value, key])
  );

  // 替换节点中的关键字
  if (json_data.graph?.nodes) {
    json_data.graph.nodes.forEach(node => {
      if (node.data?.params) {
        replaceKeysInObject(node.data.params, reverseKeyMap);
      }
    })
  }
  // 删除edge中的无用字段
  if (json_data.graph?.edges) {
    json_data.graph.edges.forEach(edge => {
      delete edge.markerEnd;
      delete edge.style;
    });
  }
}

// 拖拽放置处理
const onDrop = (event) => {
  const { dataTransfer, clientX, clientY } = event
  const nodeType = dataTransfer?.getData('application/vueflow')
  console.info(nodeType, nodeTypeConfigs[nodeType])
  if (nodeType && nodeTypeConfigs[nodeType]) {
    const position = screenToFlowCoordinate({
      x: clientX,
      y: clientY,
    })
    
    const newNode = {
      id: `${nodeIdCounter++}`,
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

// 拖拽悬停处理
const onDragOver = (event) => {
  event.preventDefault()
  if (event.dataTransfer) {
    event.dataTransfer.dropEffect = 'move'
  }
}

// 确保流程图在容器内居中显示
const onPaneReady = () => {
  setTimeout(() => {
    fitView({ padding: 0.25 })
  }, 100)
}

// 更新节点数据
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
        // 更新node的title
        node.data.label = newValue
      }
    }
  }
}

// // 同步 code 到所有连接的信号节点
// const syncCodeToConnectedSignalNodes = (quoteInputNodeId, codeValue) => {
//   const connectedEdges = getEdges.value.filter(e => e.source === quoteInputNodeId)
//   for (const edge of connectedEdges) {
//     const targetNode = getNodes.value.find(n => n.id === edge.target)
//     if (targetNode && targetNode.data.nodeType === 'signal') {
//       updateNodeData(edge.target, 'code', codeValue)
//     }
//   }
// }
// 从 QuoteInput 节点同步 code 到信号节点
// const syncCodeFromQuoteInput = (signalNodeId, quoteInputNodeId) => {
//   const signalNode = getNodes.value.find(n => n.id === signalNodeId)
//   const quoteNode = getNodes.value.find(n => n.id === quoteInputNodeId)

//   if (!signalNode || !quoteNode) return
//   if (signalNode.data.nodeType !== 'signal') return
//   if (quoteNode.data.nodeType !== 'input') return

//   // 从 QuoteInput 节点获取代码值
//   const codeParam = quoteNode.data.params['code']
//   if (!codeParam) return

//   let codeValue = codeParam.value
//   // 如果 codeValue 是数组，转为逗号分隔的字符串
//   if (Array.isArray(codeValue)) {
//     codeValue = codeValue.join(',')
//   }

//   // 更新信号节点的 code 参数
//   updateNodeData(signalNodeId, 'code', codeValue)
// }

// 从 input 节点出发，沿连接方向查找所有可达的 signal 节点（BFS 遍历）
const findSignalNodesFromInput = (inputNodeId, edges, nodes) => {
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

// 同步 code 从 input 节点到所有可达的 signal 节点
const syncCodeToDownstreamSignals = (inputNode, edges, nodes) => {
  const codeValue = inputNode.data.params['代码']?.value
  if (!codeValue) return

  // 将 code 转换为带交易所前缀的格式
  const normalizedCode = normalizeCode(codeValue)

  const signalNodes = findSignalNodesFromInput(inputNode.id, edges, nodes)
  for (const signalNode of signalNodes) {
    updateNodeData(signalNode.id, '代码', normalizedCode)
  }
}

// 将股票代码转换为带交易所前缀的格式 (eg: 000001 -> sz.000001, 600519 -> sh.600519)
const normalizeCode = (code) => {
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

// 处理边删除，当信号节点与 QuoteInput 断开连接时，清空 code
const onEdgesDelete = (deletedEdges) => {
  // for (const edge of deletedEdges) {
  //   const targetNode = getNodes.value.find(n => n.id === edge.target)
  //   if (targetNode && targetNode.data.nodeType === 'signal') {
  //     // 检查是否还有其他输入节点连接到该信号节点
  //     const connectedEdges = getEdges.value.filter(e =>
  //       e.target === edge.target &&
  //       e.source !== edge.source
  //     )
  //     const hasOtherInputConnection = connectedEdges.some(e => {
  //       const sourceNode = getNodes.value.find(n => n.id === e.source)
  //       return sourceNode && sourceNode.data.nodeType === 'input'
  //     })

  //     // 如果没有其他输入节点连接，清空 code
  //     if (!hasOtherInputConnection) {
  //       updateNodeData(edge.target, 'code', '')
  //     }
  //   }
  // }
}

// 边点击事件
const onEdgeClick = (event) => {
  const { edge, event: mouseEvent } = event
  
  // 如果按住了 Ctrl 或 Cmd 键，则切换选择状态
  if (mouseEvent.ctrlKey || mouseEvent.metaKey) {
    toggleEdgeSelection(edge)
  } else {
    // 如果没有按修饰键，则清空选择并选择当前边
    clearEdgeSelection()
    selectEdge(edge)
  }
}

// 选择边
const selectEdge = (edge) => {
  if (!selectedEdges.value.find(e => e.id === edge.id)) {
    addSelectedEdges([edge])
  }
}

// 取消选择边
const deselectEdge = (edge) => {
  removeSelectedEdges([edge])
}

// 切换边选择状态
const toggleEdgeSelection = (edge) => {
  if (selectedEdges.value.find(e => e.id === edge.id)) {
    deselectEdge(edge)
  } else {
    selectEdge(edge)
  }
}

// 清空边选择
const clearEdgeSelection = () => {
  removeSelectedEdges(selectedEdges.value)
}

// 删除选中的边
const deleteSelectedEdges = () => {
  if (selectedEdges.value.length === 0) return
  
  const edgeIdsToDelete = selectedEdges.value.map(edge => edge.id)
  removeEdges(edgeIdsToDelete)
  
  // 清空选择
  clearEdgeSelection()
}

// 保存流程图到localStorage
const saveFlow = async () => {
  if (currentStrategyId.value) {
    // 已有策略 → 直接保存为新版本（使用默认备注：当前时间）
    createNewVersion()
  } else {
    // 无策略 → 先新建策略，再保存版本
    const result = await promptDialogRef.value?.show({
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
    createNewVersion()
  }
}

// 创建新版本（接受备注，可选）
const createNewVersion = async (remark) => {
  if (!currentStrategyId.value) {
    message.error('未关联任何策略，无法保存版本')
    return
  }

  const flowData = {
    nodes: getNodes.value,
    edges: getEdges.value
  }
  console.info('[createNewVersion] 准备保存 flowData:', flowData)
  console.info(`[createNewVersion] 当前策略 ID: ${currentStrategyId.value}`)

  try {
    const versionId = await historyStore.addVersion(
      currentStrategyId.value,
      flowData,
      remark
    )
    console.info(`[createNewVersion] 版本已保存：${versionId}`)
    currentVersionId.value = versionId
    hasUnsavedChanges.value = false
    message.success('版本保存成功')
  } catch (error) {
    console.error('[createNewVersion] 保存失败:', error)
    message.error('保存失败')
  }
}

const formatDateForRemark = (date) => {
  const year = date.getFullYear()
  const month = String(date.getMonth() + 1).padStart(2, '0')
  const day = String(date.getDate()).padStart(2, '0')
  const hour = String(date.getHours()).padStart(2, '0')
  const minute = String(date.getMinutes()).padStart(2, '0')
  return `${year}-${month}-${day} ${hour}:${minute}`
}

const saveAsNewVersion = async () => {
  // 如果无策略，先新建
  if (!currentStrategyId.value) {
    const result = await promptDialogRef.value?.show({
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
  const result = await promptDialogRef.value?.show({
    title: '版本备注',
    placeholder: '请输入版本备注（留空将使用当前时间）',
    defaultValue: defaultRemark
  })
  if (result?.cancelled) return
  createNewVersion(result.value.trim() || undefined)
}

// 从localStorage加载保存的流程图
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

      console.info('loadedNodes:', loadedNodes)

      // 更新节点计数器
      if (loadedNodes.length > 0) {
        const maxId = Math.max(...loadedNodes.map(node => parseInt(node.id) || 0))
        nodeIdCounter = maxId + 1
      }
      // 然后添加边（使用 nextTick 确保节点已添加）
      nextTick(() => {
        // 验证并添加边
        const validEdges = loadedEdges.filter(edge => {
          const sourceExists = getNodes.value.some(n => n.id === edge.source)
          const targetExists = getNodes.value.some(n => n.id === edge.target)
          console.info('edge:', edge)
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

// 重新载入流程图
const showHistoryStrategy = () => {
  // if (confirm('确定要重新载入上次保存的流程图吗？当前未保存的更改将会丢失。')) {
  //   loadSavedFlow()
  //   // 重新适应视图
  //   setTimeout(() => {
  //     fitView({ padding: 0.25 })
  //   }, 100)
  // }
  emit('show-history') // 触发显示历史策略面板事件
}

const showStrategyNodes = () => {
  emit('show-flow-components') // 触发显示节点组件面板事件
}

// 验证流程图有效性
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

// 确保有版本 ID（用于保存回测结果）
const ensureVersionId = async () => {
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

const runBacktest = async () => {
  // 1. 验证流程图
  if (!validateFlow()) {
    return
  }

  // 2. 确保有版本 ID 用于保存回测结果
  const versionId = await ensureVersionId()
  if (!versionId) {
    message.error('无法创建版本，回测中止')
    return
  }

  // 3. 同步 input 节点的 code 到所有可达的 signal 节点（确保回测前数据一致）
  const inputNodes = getNodes.value.filter(n => n.data.nodeType === 'input')
  for (const inputNode of inputNodes) {
    syncCodeToDownstreamSignals(inputNode, getEdges.value, getNodes.value)
  }

  // 4. 获取当前图节点信息（使用动态生成的策略信息）
  const strategyName = currentStrategyId.value
    ? strategies.value.find(s => s.id === currentStrategyId.value)?.name || '未命名策略'
    : '临时策略'

  const curGraph = {
    id: currentStrategyId.value ? `strategy_${currentStrategyId.value}` : `temp_${Date.now()}`,
    name: strategyName,
    description: '用户自定义策略',
    nodes: getNodes.value,
    edges: getEdges.value
  }
  let graph = JSON.stringify(curGraph)
  // 替换中文
  for (const key in keyMap) {
    const src = keyMap[key]
    graph = graph.replaceAll(src, key)
  }
  console.info('graph:', graph)

  try {
    const response = await axios.post('/v0/backtest', {script: graph})
    console.info('backtest result:', response)

    const result = response.data

    // 4. 传递指标数据到 ReportView
    try {
      if (reportViewRef.value && reportViewRef.value.updateMetrics) {
        reportViewRef.value.updateMetrics(result.features || {})
        console.info('策略指标数据已传递给报表组件')
      } else {
        console.warn('ReportView 组件未找到 updateMetrics 方法')
      }
    } catch (metricsError) {
      console.error('传递指标数据时出错:', metricsError)
    }

    // 5. 传递回测日期范围到 ReportView（用于获取基准数据）
    try {
      const buySignals = result.buy || []
      const sellSignals = result.sell || []
      const allSignals = [...buySignals, ...sellSignals]

      if (allSignals.length > 0) {
        const timestamps = allSignals.map(s => s[1])
        const minTime = Math.min(...timestamps)
        const maxTime = Math.max(...timestamps)
        const startDate = new Date(minTime * 1000)
        const endDate = new Date(maxTime * 1000)

        const benchmarkSymbol = localStorage.getItem('benchmark_symbol') || 'SH000300'

        if (reportViewRef.value && reportViewRef.value.updateBenchmark) {
          reportViewRef.value.updateBenchmark({
            symbol: benchmarkSymbol,
            name: '',
            startDate,
            endDate
          })
          console.info(`回测日期范围已传递给报表组件：${startDate.toISOString().split('T')[0]} ~ ${endDate.toISOString().split('T')[0]}`)
        }
      }
    } catch (dateError) {
      console.warn('传递回测日期范围时出错:', dateError)
    }

    // 6. 解析回测结果中的交易历史数据
    try {
      const buySignals = result.buy || []
      const sellSignals = result.sell || []

      const formatSignals = (signals) => {
        return signals.map(signal => {
          const timestamp = signal[1]
          const price = signal[3]
          const date = new Date(timestamp * 1000)
          const Y = date.getFullYear() + '-'
          const M = (date.getMonth() + 1 < 10 ? '0' + (date.getMonth() + 1) : date.getMonth() + 1) + '-'
          const D = date.getDate()
          return [Y + M + D, price]
        })
      }

      if (reportViewRef.value && reportViewRef.value.updateTradeSignals) {
        reportViewRef.value.updateTradeSignals(
          formatSignals(buySignals),
          formatSignals(sellSignals)
        )
        console.info(`交易历史数据已传递给报表组件：买入${buySignals.length}条，卖出${sellSignals.length}条`)
      } else {
        console.warn('ReportView 组件未找到 updateTradeSignals 方法')
      }
    } catch (parseError) {
      console.error('解析回测交易历史数据时出错:', parseError)
    }

    // 7. 保存回测结果到 historyStore
    try {
      const backtestResult = {
        backtestTime: new Date().toISOString(),
        features: result.features || {},
        summary: result.summary || {},
        buy: result.buy || [],
        sell: result.sell || [],
        script: graph
      }
      historyStore.saveBacktestResult(versionId, backtestResult)
      console.info(`回测结果已保存到版本 ${versionId}`)
    } catch (saveError) {
      console.error('保存回测结果失败:', saveError)
    }

    // 8. 显示回测完成消息
    const summary = result.summary || {}
    const buyCount = summary.buy_count || (result.buy?.length || 0)
    const sellCount = summary.sell_count || (result.sell?.length || 0)
    const indicatorCount = summary.indicator_count || Object.keys(result.features || {}).length

    addInfoMessage(`回测完成：${buyCount}笔买入，${sellCount}笔卖出，${indicatorCount}个指标`, 'success')

    // 9. 所有处理完成后再切换 Tab
    // nextTick(() => {
    //   activeTab.value = 'backtest'
    // })

  } catch (error) {
    console.error('回测失败:', error)
    const exceptionWhat = error.response?.headers?.['exception_what'] ||
                           error.response?.headers?.['EXCEPTION_WHAT'] ||
                           error.response?.headers?.['Exception-What'] ||
                           error.message ||
                           '未知错误'

    const status = error.response?.status
    let userMessage = '回测失败'
    if (status === 400) {
      userMessage = '策略脚本格式错误，请检查节点配置'
    } else if (status === 404) {
      userMessage = '策略文件未找到'
    } else if (status === 500) {
      userMessage = `服务器错误：${exceptionWhat}`
    } else {
      userMessage = `错误：${exceptionWhat}`
    }

    message.error(userMessage)
    addInfoMessage(userMessage, 'error')
    return
  }

    // 10. 获取信号节点的代码和日期范围
  let signalNode = null
  for (const node of getNodes.value) {
    if (node.data.nodeType === 'signal') {
      signalNode = node
      break
    }
  }

  if (signalNode) {
    const codes = signalNode.data.params['代码']['value']
    const symbols = codes.split(',')
    const rangeDate = signalNode.data.params['回测周期']['value']

    if (symbols.length > 0 && rangeDate && rangeDate.length === 2) {
      // 等待 Tab 切换和图表初始化完成后再获取历史数据
      nextTick(async () => {
        // 等待 ReportView 组件完成初始化
        await new Promise(resolve => setTimeout(resolve, 500))

        // 调用 ReportView 的 initializeCharts 确保图表已初始化
        if (reportViewRef.value && reportViewRef.value.initializeCharts) {
          reportViewRef.value.initializeCharts()
        }

        // 然后再获取历史价格数据
        if (reportViewRef.value && reportViewRef.value.updatePrice) {
          reportViewRef.value.updatePrice(symbols[0], rangeDate[0], rangeDate[1])
        }
      })
    }
  } else {
    console.warn('未找到信号节点，无法更新价格图表')
    addInfoMessage('未找到信号节点，价格图表将不会更新', 'warning')
  }
}

// 从历史版本加载流程图
const loadVersionFromHistory = async (version) => {
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
      nodeIdCounter = maxId + 1
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
      if (reportViewRef.value && reportViewRef.value.loadBacktestResultFromVersion) {
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

defineExpose({
  runBacktest,
  loadVersionFromHistory
})
</script>
<style scoped>
  /* 头部容器样式 */
.tab-header {
    display: flex;
    justify-content: space-between;
    align-items: center;
    background-color: var(--darker-bg);
    border-bottom: 1px solid var(--border);
    padding: 0 16px;
}
/* 添加上下文菜单样式 */
.context-menu {
    position: fixed;
    background: var(--panel-bg);
    border: 1px solid var(--border);
    border-radius: 8px;
    box-shadow: 0 4px 12px rgba(0, 0, 0, 0.3);
    z-index: 10000;
    min-width: 150px;
    padding: 8px 0;
    backdrop-filter: blur(10px);
}

.context-menu-item {
    padding: 8px 16px;
    cursor: pointer;
    display: flex;
    align-items: center;
    gap: 8px;
    font-size: 14px;
    color: var(--text);
    transition: background-color 0.2s ease;
}
/* 流程图操作按钮容器 */
.flow-actions {
    position: absolute;
    bottom: 20px;
    right: 20px;
    display: flex;
    flex-direction: column;
    gap: 10px;
    z-index: 1000;
    pointer-events: auto;
}
.context-menu-item:hover {
    background-color: var(--primary);
    color: white;
}

.context-menu-item i {
    width: 16px;
    text-align: center;
}

.context-menu-divider {
    height: 1px;
    background-color: var(--border);
    margin: 4px 0;
}

.main-container {
    height: 100%;
    display: flex;
    flex-direction: column;
    background-color: var(--dark-bg);
}
/* 功能按钮样式 */
.action-buttons {
    display: flex;
    gap: 8px;
}
.action-btn {
    display: flex;
    align-items: center;
    justify-content: flex-start;
    gap: 8px;
    padding: 10px 16px;
    background: rgba(30, 33, 45, 0.85); /* 增加透明度 */
    color: var(--text);
    border: 1px solid rgba(255, 255, 255, 0.1); /* 更细更透明的边框 */
    border-radius: 8px; /* 稍微增加圆角 */
    font-size: 14px;
    cursor: pointer;
    transition: all 0.3s ease;
    width: 110px;
    box-shadow: 0 4px 12px rgba(0, 0, 0, 0.25);
    opacity: 0.9; /* 整体透明度 */
}

.action-btn:hover {
    background: var(--primary);
    color: white;
    border-color: var(--primary);
    transform: translateY(-2px);
    box-shadow: 0 4px 12px rgba(41, 98, 255, 0.3);
    opacity: 1;
}

.action-btn:active {
    transform: translateY(0);
}

.action-btn i {
    font-size: 12px;
}
/* 选项卡样式 */
.tabs {
    display: flex;
    background-color: var(--darker-bg);
    border-bottom: 1px solid var(--border);
    flex: 1;
}

.tab-button {
    padding: 12px 24px;
    background: none;
    border: none;
    font-size: 14px;
    cursor: pointer;
    color: var(--text-secondary);
    border-bottom: 2px solid transparent;
    transition: all 0.3s ease;
    display: flex;
    align-items: center;
    gap: 8px;
}

.tab-button:hover {
    color: var(--text);
    background: rgba(41, 98, 255, 0.1);
}

.tab-button.active {
    color: var(--primary);
    border-bottom-color: var(--primary);
    background: rgba(41, 98, 255, 0.15);
}

/* 流程图面板 */
.flow-panel {
    flex: 1;
    display: flex;
    background-color: var(--dark-bg);
    border-radius: 10px;
    border: 1px solid var(--border);
    overflow: hidden;
    box-shadow: 0 4px 12px rgba(0, 0, 0, 0.15);
}
/* 添加包装容器样式 */
.flow-container-wrapper {
  position: relative;
  width: 100%;
  height: 100%;
  flex: 1;
}

.flow-container {
    flex: 1;
    height: 100%;
    position: relative;
}

/* 自定义节点样式 */
.vue-flow__node-custom {
    padding: 10px;
    border-radius: 8px;
    border: 2px solid var(--primary);
    background: var(--panel-bg);
    box-shadow: 0 2px 5px rgba(0, 0, 0, 0.2);
    min-width: 150px;
    font-size: 0.9rem;
    color: var(--text);
}

.vue-flow__node-custom.selected {
    border-color: rgba(205, 87, 41, 0.892) !important;
    box-shadow: 0 0 5px 1px rgba(205, 109, 0, 0.8);
}

/* 多选状态样式 */
.vue-flow__node-custom.multi-selected {
    border-width: 3px;
    border-color: rgba(205, 87, 41, 0.892) !important;
    box-shadow: 0 0 5px 1px rgba(208, 109, 0, 0.8);
    transform: translateY(-1px);
    z-index: 1000;
}

.node-header {
    display: flex;
    align-items: center;
    margin-bottom: 8px;
    padding-bottom: 8px;
    border-bottom: 1px solid var(--border);
}

.node-icon {
    width: 24px;
    height: 24px;
    border-radius: 50%;
    display: flex;
    align-items: center;
    justify-content: center;
    margin-right: 10px;
    color: white;
    font-size: 12px;
    background-color: var(--primary);
}

.node-title {
    font-weight: 600;
    color: var(--text);
}

.node-content {
    padding: 5px 0;
}

.node-param {
    font-size: 0.8rem;
    margin: 3px 0;
    color: var(--text-secondary);
}

.vue-flow__edge-path {
    stroke: var(--border);
    stroke-width: 2;
}

.vue-flow__edge.selected .vue-flow__edge-path {
    stroke: var(--accent);
    stroke-width: 3;
}

.vue-flow__edge.animated path {
    stroke-dasharray: 5;
    animation: dashdraw 0.5s linear infinite;
}

@keyframes dashdraw {
    from { stroke-dashoffset: 10; }
}

.vue-flow__connectionline {
    z-index: 1000;
}

/* 回测结果面板 */
.backtest-panel {
    flex: 1;
    display: flex;
    padding: 20px;
    overflow-y: auto;
    width: 100%;
    scrollbar-width: thin;
    scrollbar-color: var(--primary) transparent;
}

.backtest-container {
    flex: 1;
    display: flex;
    flex-direction: column;
    gap: 20px;
}

/* 关键指标卡片 */
.metrics-grid {
    display: grid;
    grid-template-columns: repeat(3, 1fr);
    gap: 16px;
}

.metric-card {
    background-color: var(--panel-bg);
    border-radius: 10px;
    padding: 20px;
    border: 1px solid var(--border);
    box-shadow: 0 4px 12px rgba(0, 0, 0, 0.15);
    text-align: center;
}

.metric-title {
    font-size: 14px;
    color: var(--text-secondary);
    margin-bottom: 8px;
}

.metric-value {
    font-size: 24px;
    font-weight: 700;
    margin-bottom: 4px;
}

.metric-value.positive {
    color: var(--secondary);
}

.metric-value.negative {
    color: #ef4444;
}

.metric-description {
    font-size: 12px;
    color: var(--text-secondary);
}

/* 图表区域 */
.charts-container {
    display: flex;
    flex-direction: column;
    gap: 20px;
}

.chart-section {
    background-color: var(--panel-bg);
    border-radius: 10px;
    padding: 20px;
    border: 1px solid var(--border);
    box-shadow: 0 4px 12px rgba(0, 0, 0, 0.15);
}

.chart-title {
    margin-top: 0;
    margin-bottom: 16px;
    color: var(--text);
    font-size: 18px;
}

.chart-wrapper {
    position: relative;
}

.chart-legend {
    display: flex;
    gap: 16px;
    margin-bottom: 10px;
}

.legend-item {
    display: flex;
    align-items: center;
    gap: 6px;
    font-size: 14px;
    color: var(--text-secondary);
}

.legend-color {
    width: 12px;
    height: 12px;
    border-radius: 2px;
}

.price-color {
    background-color: var(--primary);
}

.buy-color {
    background-color: var(--secondary);
}

.sell-color {
    background-color: #ef4444;
}

.strategy-color {
    background-color: var(--primary);
}

.benchmark-color {
    background-color: var(--text-secondary);
}

.chart-placeholder {
    height: 300px;
    background-color: var(--darker-bg);
    border-radius: 6px;
    display: flex;
    align-items: center;
    justify-content: center;
    border: 1px solid var(--border);
}

/* 模拟图表样式 */
.mock-chart {
    width: 100%;
    height: 100%;
    display: flex;
    flex-direction: column;
    padding: 20px;
}

.mock-chart-title {
    text-align: center;
    margin-bottom: 20px;
    color: var(--text-secondary);
    font-size: 16px;
}

.mock-chart-content {
    flex: 1;
    position: relative;
    border-bottom: 1px solid var(--border);
    border-left: 1px solid var(--border);
}

.mock-line {
    position: absolute;
    top: 30%;
    left: 0;
    right: 0;
    height: 2px;
    background-color: var(--primary);
}

.strategy-line {
    background-color: var(--primary);
    top: 40%;
}

.benchmark-line {
    background-color: var(--text-secondary);
    top: 60%;
}

.mock-signal {
    position: absolute;
    top: 0;
    width: 10px;
    height: 10px;
    border-radius: 50%;
    transform: translateY(-50%);
}

.mock-signal.buy {
    background-color: var(--secondary);
}

.mock-signal.sell {
    background-color: #ef4444;
}

.mock-position-bar {
    position: absolute;
    bottom: 0;
    width: 18%;
    background-color: var(--accent);
    opacity: 0.7;
}

.mock-position-bar:nth-child(1) {
    left: 0;
    height: 70%;
}

.mock-position-bar:nth-child(2) {
    left: 20%;
    height: 40%;
}

.mock-position-bar:nth-child(3) {
    left: 40%;
    height: 80%;
}

.mock-position-bar:nth-child(4) {
    left: 60%;
    height: 60%;
}

.mock-position-bar:nth-child(5) {
    left: 80%;
    height: 30%;
}

/* 响应式布局 */
@media (max-width: 1200px) {
    .metrics-grid {
        grid-template-columns: repeat(2, 1fr);
    }
}

@media (max-width: 768px) {
    .metrics-grid {
        grid-template-columns: 1fr;
    }
    
    .tabs {
        padding: 0 10px;
    }
    
    .tab-button {
        padding: 10px 16px;
        font-size: 14px;
    }
}

/* 控制按钮样式 */
.control-btn.small {
    padding: 6px 10px;
    font-size: 12px;
}

.chart-controls {
    display: flex;
    gap: 8px;
}
</style>