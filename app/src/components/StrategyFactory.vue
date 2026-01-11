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
          <div class="info-panel" :class="{ 'collapsed': isInfoPanelCollapsed }">
            <div class="info-panel-content" ref="infoPanelContent">
              <div 
                v-for="(msg, index) in infoMessages" 
                :key="index" 
                class="info-message"
                :class="`type-${msg.type}`"
              >
                <span class="info-timestamp">{{ formatTime(msg.timestamp) }}</span>
                <span class="info-text">{{ msg.text }}</span>
              </div>
            </div>
          </div>

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
            <button class="action-btn" @click="newFlow" title="新建流程图">
                <i class="fas fa-plus"></i>
                新建
            </button>
            <button class="action-btn" @click="saveFlow" title="保存流程图">
                <i class="fas fa-save"></i>
                保存
            </button>
            <button class="action-btn" @click="reloadFlow" title="重新载入流程图">
                <i class="fas fa-redo"></i>
                重新载入
            </button>
            <!-- <button class="action-btn run-btn" @click="runBacktest" title="运行回测">
                <i class="fas fa-play"></i>
                运行回测
            </button> -->
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
  </div>
</template>

<script setup>
import { ref, onMounted, computed, onUnmounted, provide, watch, nextTick } from 'vue'
import { useVueFlow, VueFlow, MarkerType } from '@vue-flow/core'
import { message } from '@/tool'
import FlowNode from './flow/FlowNode.vue'
import FlowConnectLine from './flow/FlowConnectLine.vue'
import ReportView from './ReportView.vue'
import axios from 'axios'
import sseService from '@/ts/SSEService';

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
// localStorage 键名
const FLOW_STORAGE_KEY = 'vue-flow-saved-strategy'
const LAST_BACKTEST_RESULT = 'last_backtest_result'
let nodeIdCounter = 10

// 信息面板相关状态
const infoMessages = ref([])
const isInfoPanelCollapsed = ref(false)
const infoPanelContent = ref(null)
const MAX_MESSAGES = 100 // 最大消息数量

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
  infoMessages.value.push({
    text,
    type, // 'info', 'warning', 'error'
    timestamp
  })
  
  // 限制消息数量
  if (infoMessages.value.length > MAX_MESSAGES) {
    infoMessages.value = infoMessages.value.slice(-MAX_MESSAGES)
  }
  
  // 滚动到底部
  nextTick(() => {
    if (infoPanelContent.value) {
      infoPanelContent.value.scrollTop = infoPanelContent.value.scrollHeight
    }
  })
}

// 清空消息
const clearMessages = () => {
  infoMessages.value = []
}

// 切换信息面板折叠状态
const toggleInfoPanel = () => {
  isInfoPanelCollapsed.value = !isInfoPanelCollapsed.value
}

// 格式化时间显示
const formatTime = (date) => {
  return date.toLocaleTimeString('zh-CN', { 
    hour12: false,
    hour: '2-digit',
    minute: '2-digit',
    second: '2-digit'
  })
}

// 添加全局点击事件监听
onMounted(() => {
    document.addEventListener('click', closeContextMenu)
    document.addEventListener('keydown', onKeyDown)
    // loadSavedFlow()
    sseService.on('strategy', onStrategyMessageUpdate)
})

onUnmounted(() => {
    document.removeEventListener('click', closeContextMenu)
    document.removeEventListener('keydown', onKeyDown)
    sseService.off('strategy')
})

// 提供 selectedNodes 给子组件
provide('selectedNodes', selectedNodes)
// 提供 selectedEdges 给子组件
provide('selectedEdges', selectedEdges)

// 监听 Vue Flow 的选中状态变化
watch(getSelectedNodes, (newSelectedNodes) => {
    // 同步 Vue Flow 的选中状态到我们的 selectedNodes
    selectedNodes.value = newSelectedNodes
}, { deep: true })

// 监听选中的边变化
watch(getSelectedEdges, (newSelectedEdges) => {
  selectedEdges.value = newSelectedEdges
}, { deep: true })

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
const onSelectionDragStart = () => {
    console.log('开始拖动选择')
}

const onSelectionDrag = () => {
    console.log('拖动选择中')
}

const onSelectionDragStop = () => {
    console.log('停止拖动选择')
}

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

const keyMap = {
  "source": "来源",
  "code": "代码",
  "formula": "公式",
  "method": "方法",
  "smoothTime": "平滑时间",
  "indicator": "输出指标",
  "sharp": "夏普比率",
  "maxDrawdown": "最大回撤",
  "totalReturn": "总收益",
  "annualReturn": "年化收益",
  "winRate": "胜率",
  "numTrades": "交易次数",
  "CalmarRatio": "卡玛比率",
  "InformationRatio": "信息比率",
  "BacktestRange": "回测周期",
  "freq": "频率",
  "buy": "买入条件",
  "sell": "卖出条件",
  "1d": "日频",
  "VaR": "VaR",
  "ES": "ES",
  "initialCapital": "初始资金",
  "commission": "佣金费率",
  "stampDuty": "印花税率",
  "minFee": "最低手续费",
  "slippage": "滑点",
}

// 定义节点类型配置
const nodeTypeConfigs = {
  'data-source': {
    "label": "数据输入",
    "nodeType": "input",
    "params": {
      "来源": {
        "value": "股票",
        "type": "select",
        "options": ["股票", "期货"]
      },
      "代码": {
        "value": ["000001"],
        "type": "text"
      },
      "close": {
        "value": "close",
        "type": "text"
      },
      "open": {
        "value": "open",
        "type": "text"
      },
      "high": {
        "value": "high",
        "type": "text"
      },
      "low": {
        "value": "low",
        "type": "text"
      },
      "volume": {
        "value": "volume",
        "type": "text"
      }
    }
  },
  'signal-generation': {
    "label": "交易信号生成",
    "nodeType": "signal",
    "params": {
      "类型": {
        "value": "股票",
        "type": "select",
        "options":["股票", "期货", "期权"]
      },
      "买入条件": {
        "value": "MA_5-MA_15 >= 0",
        "type": "text"
      },
      "卖出条件": {
        "value": "MA_5-MA_15 < 0",
        "type": "text"
      }
    }
  },
  'trade': {
    "label": "执行交易",
    "nodeType": "trade",
    "params": {
      "初始资金": {
        "value": 100000,
        "type": "number",
        "unit": "元"
      },
      "佣金费率": {
        "value": 0.0003,
        "type": "number",
        "min": 0,
        "max": 0.01,
        "step": 0.0001,
        "unit": "%"
      },
      "印花税率": {
        "value": 0.001,
        "type": "number", 
        "min": 0,
        "max": 0.01,
        "step": 0.0001,
        "unit": "%"
      },
      "最低手续费": {
        "value": 5,
        "type": "number",
        "min": 0,
        "max": 50,
        "step": 1,
        "unit": "元"
      },
      "滑点": {
        "value": 0.001,
        "type": "number",
        "min": 0,
        "max": 0.01,
        "step": 0.0001
      },
      "类型": {
        "value": "股票",
        "type": "select",
        "options":["股票", "期货", "期权"]
      },
      "策略": {
        "value": "立即执行(市价单)",
        "type": "select",
        "options":["立即执行(市价单)", "立即执行(限价单)", "VWAP"]
      },
      "回测周期": {
        "value": ["2020-01-01", "2023-12-31"], // 合并为日期范围
        "type": "daterange"
      }
    }
  },
  'debug': {
    "label": "调试",
    "nodeType": "debug",
    "params": {
      '下载路径': {
        "value": "",
        "type": "directory"
      },
      '下载文件': {
        "value": "",
        "type": "download"
      }
    }
  },
  'risk': {},
  'porfolio': {

  },
  'cnn': {
    label: 'CNN模型',
    nodeType: 'backtest',
    params: {
      
    }
  },
  'xgboost': {
    label: 'xgboost',
    nodeType: 'xgboost',
    params: {
      '上传模型': {
        "value": "",
        "type": "file"
      },
    }
  },
  'basic-index': {
    "label": "MA_5",
    "nodeType": "function",
    "params": {
      "方法": {
        "value": "MA",
        "type": "select",
        "options": ["MA"]
      },
      "平滑时间": {
        "value": 5,
        "type": "text",
        "unit": "天"
      }
    },

  }
}

function onStrategyMessageUpdate(message) {
  console.info('onStrategyMessageUpdate message:', message)
  const data = message.data
  addInfoMessage(data.message, data.type)
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

// 新建流程图
const newFlow = () => {
  if (confirm('确定要新建流程图吗？当前未保存的更改将会丢失。')) {
    removeNodes(getNodes.value.map(e=>e.id))
    removeEdges(getEdges.value.map(e => e.id))
    nodeIdCounter = 1
  }
}
// 保存流程图到localStorage
const saveFlow = () => {
  try {
    const flowData = {
      nodes: getNodes.value,
      edges: getEdges.value,
      saveTime: new Date().toISOString()
    }
    localStorage.setItem(FLOW_STORAGE_KEY, JSON.stringify(flowData))
    message.success('流程图已保存到本地存储')
  } catch (error) {
    console.error('保存流程图失败:', error)
    message.error('保存流程图失败')
  }
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
const reloadFlow = () => {
  if (confirm('确定要重新载入上次保存的流程图吗？当前未保存的更改将会丢失。')) {
    loadSavedFlow()
    // 重新适应视图
    setTimeout(() => {
      fitView({ padding: 0.25 })
    }, 100)
  }
}

const runBacktest = async () => {
  // 获取当前图节点信息
  const curGraph = {
    "id": "graph_ma2",
    "name": "双均线动量流水线",
    "description": "包含数据输入、特征工程、信号输出和结果输出的完整流水线",
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
    // localStorage.setItem(LAST_BACKTEST_RESULT, response)
  } catch (error) {
    const exceptionWhat = error.response.headers['exception_what'] || 
                           error.response.headers['EXCEPTION_WHAT'] ||
                           error.response.headers['Exception-What']
    message.error(`运行失败: ${exceptionWhat}`)
  }

  // 获取信号节点的代码和日期范围
  let signalNode = null
  for (const node of getNodes.value) {
    if (node.data.nodeType === 'signal') {
      signalNode = node
      break
    }
  }
  const codes = signalNode.data.params['代码']['value']
  const symbols = codes.split(',')
  const rangeDate = signalNode.data.params['回测周期']['value']
  if (symbols.length != 0) {
    reportViewRef.value.updatePrice(symbols[0], rangeDate[0], rangeDate[1]);
  }
}

defineExpose({
  runBacktest
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

/* 信息面板样式 */
.info-panel {
    position: absolute;
    bottom: 20px;
    left: 20px;
    width: 320px;
    max-height: 300px;
    background: transparent; /* 半透明背景 */
    backdrop-filter: none;
    -webkit-backdrop-filter: none;
    border: none;
    z-index: 1000;
    display: flex;
    flex-direction: column;
    transition: all 0.3s ease;
    overflow: hidden;
}

.info-panel.collapsed {
    height: 40px;
    max-height: 40px;
}

.info-panel-header {
    display: flex;
    justify-content: space-between;
    align-items: center;
    padding: 10px 12px;
    background: rgba(41, 98, 255, 0.1);
    border-bottom: 1px solid rgba(255, 255, 255, 0.1);
    cursor: pointer;
    user-select: none;
    transition: background-color 0.2s ease;
}

.info-panel-header:hover {
    background: rgba(41, 98, 255, 0.15);
}

.info-panel-header span {
    display: flex;
    align-items: center;
    gap: 8px;
    font-size: 14px;
    font-weight: 500;
    color: var(--primary);
}

.info-panel-actions {
    display: flex;
    gap: 8px;
}

.info-panel-btn {
    width: 24px;
    height: 24px;
    display: flex;
    align-items: center;
    justify-content: center;
    background: transparent;
    border: 1px solid rgba(255, 255, 255, 0.1);
    border-radius: 4px;
    color: var(--text-secondary);
    cursor: pointer;
    transition: all 0.2s ease;
    font-size: 12px;
}

.info-panel-btn:hover {
    background: var(--primary);
    color: white;
    border-color: var(--primary);
}

.info-panel-content {
    flex: 1;
    overflow-y: auto;
    padding: 8px;
    font-size: 12px;
    scrollbar-width: thin;
    scrollbar-color: var(--primary) transparent;
    background: rgba(30, 33, 45, 0.1);
}

.info-panel-content::-webkit-scrollbar {
    width: 6px;
}

.info-panel-content::-webkit-scrollbar-track {
    background: transparent;
}

.info-panel-content::-webkit-scrollbar-thumb {
    background-color: var(--primary);
    border-radius: 3px;
}
.info-message {
    display: flex;
    gap: 8px;
    padding: 6px 8px;
    margin-bottom: 4px;
    border-radius: 4px;
    background: transparent;
    animation: fadeIn 0.3s ease;
    line-height: 1.4;
}

@keyframes fadeIn {
    from { opacity: 0; transform: translateY(-5px); }
    to { opacity: 1; transform: translateY(0); }
}

.info-message.type-warning {
  color: #ff9800;
}

.info-message.type-error {
  color: #f44336;
}

.info-timestamp {
    color: var(--text-secondary);
    font-size: 10px;
    white-space: nowrap;
    min-width: 60px;
    padding-top: 1px;
}

.info-text {
    color: var(--text);
    flex: 1;
    word-break: break-word;
}

.type-info .info-text {
    color: var(--text);
}

.type-success .info-text {
    color: var(--secondary);
}

.type-warning .info-text {
    color: #ff9800;
}

.type-error .info-text {
    color: #f44336;
}

.info-empty {
    display: flex;
    flex-direction: column;
    align-items: center;
    justify-content: center;
    height: 100px;
    color: var(--text-secondary);
    font-size: 14px;
    gap: 8px;
}

.info-empty i {
    font-size: 24px;
    opacity: 0.5;
}
</style>