<template>
  <div class="main-container">
    <FlowTabHeader v-model="activeTab" />

    <!-- 流程图面板 -->
    <div v-show="activeTab === 'flow'" class="flow-panel">
      <div class="flow-container-wrapper">
        <InfoPanel ref="infoPanelRef" />

        <!-- 回测配置栏（顶部） -->
        <div class="backtest-config-bar">
          <label class="config-label">回测周期</label>
          <DateRangeParam
            :value="backtestRange"
            @update="updateBacktestRange"
          />
        </div>

        <FlowCanvas
          ref="flowCanvasRef"
          :nodes="getNodes"
          :edges="getEdges"
          :is-valid-connection="isValidConnection"
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
          @node-click="onNodeClick"
          @update-node="updateNodeData"
          @visualize-debug="onVisualizeDebug"
        />

        <FlowContextMenu
          :visible="contextMenu.visible"
          :x="contextMenu.x"
          :y="contextMenu.y"
          @delete-nodes="deleteSelectedNodes"
          @duplicate-nodes="duplicateSelectedNodes"
          @clear-selection="clearSelection"
        />
      </div>
    </div>

    <!-- 回测结果面板 -->
    <div v-show="activeTab === 'backtest'" class="backtest-panel">
      <ReportView ref="reportViewRef"></ReportView>
    </div>

    <PromptDialog ref="promptDialogRef" />
  </div>
</template>

<script setup>
import { ref, watch, nextTick, onMounted, onUnmounted, provide, computed, inject } from 'vue'
import ReportView from './report/ReportView.vue'
import InfoPanel from './strategy/InfoPanel.vue'
import FlowCanvas from './FlowCanvas.vue'
import FlowTabHeader from './FlowTabHeader.vue'
import FlowContextMenu from './FlowContextMenu.vue'
import PromptDialog from './PromptDialog.vue'
import DateRangeParam from './flow/params/DateRangeParam.vue'
import { message } from '@/tool'
import { usePortfolioStore } from '@/stores/portfolio'
import { useFlowState } from '@/components/strategy/composables/useFlowState'
import { useFlowOperations } from '@/components/strategy/composables/useFlowOperations'
import { useFlowSaveLoad } from '@/components/strategy/composables/useFlowSaveLoad'
import { useBacktest } from '@/components/strategy/composables/useBacktest'
import * as codeSync from '@/components/strategy/composables/useCodeSync'
import { convertLabelsToKeys } from '@/lib/nodes'

// 注入报表配置面板控制方法（从 App.vue）
const onShowReportConfig = inject('onShowReportConfig', () => {
  console.warn('[StrategyFactory] onShowReportConfig 未提供')
})
const onHideReportConfig = inject('onHideReportConfig', () => {
  console.warn('[StrategyFactory] onHideReportConfig 未提供')
})

// 初始化 portfolio store
const portfolioStore = usePortfolioStore()

// 初始化 composables
const state = useFlowState()
const {
  fitView, addNodes, addEdges, screenToFlowCoordinate, updateNode,
  getNodes, getEdges, removeNodes, removeEdges, getConnectedEdges,
  getSelectedNodes, getSelectedEdges, addSelectedNodes, addSelectedEdges,
  removeSelectedEdges, edgesSelectionActive, nodesSelectionActive,
  removeSelectedNodes, onNodesInitialized,
  selectedNodes, selectedEdges, currentStrategyId, currentVersionId,
  hasUnsavedChanges, nodeIdCounter, activeTab
} = state

// 右键菜单状态
const contextMenu = ref({
  visible: false,
  x: 0,
  y: 0,
  targetNode: null
})

// 绑定到 state
state.contextMenu = contextMenu

// 初始化 operations
const operations = useFlowOperations(state)
const {
  selectNode, deselectNode, toggleNodeSelection,
  selectEdge, deselectEdge, toggleEdgeSelection,
  clearSelection, clearEdgeSelection,
  deleteSelectedNodes, deleteSelectedEdges, duplicateSelectedNodes,
  onNodeContextMenu, onNodeClick, onPaneClick, onEdgeClick,
  onSelectionContextMenu, onDrop, onDragOver, onPaneReady,
  updateNodeData, onConnect, isValidConnection
} = operations

// 初始化 saveLoad
const saveLoad = useFlowSaveLoad(state, operations)
const {
  saveFlow, saveAsNewVersion, createNewVersion,
  loadVersionFromHistory, validateFlow, ensureVersionId,
  historyStore, strategies, versions
} = saveLoad

// 注入父组件的 currentStrategyId（用于同步部署/启动/停止按钮状态）
const parentStrategyId = inject('currentStrategyId', null)

// 监听子组件 currentStrategyId 变化，同步到父组件
watch(currentStrategyId, (newId) => {
  if (parentStrategyId) {
    parentStrategyId.value = newId
  }
})

// 回测时间范围配置（根级别，不配置则使用数据文件全范围）
const backtestRange = ref(['2020-01-01', '2025-12-31'])

// 初始化 backtest（传入 backtestRange 引用）
const backtest = useBacktest(state, saveLoad, codeSync, backtestRange)
const { runBacktest } = backtest

// Refs
const infoPanelRef = ref(null)
const flowCanvasRef = ref(null)
const reportViewRef = ref(null)
const promptDialogRef = ref(null)

const updateBacktestRange = (range) => {
  backtestRange.value = range
}

// 提供 selectedNodes/Edges 和 backtestRange 给子组件
provide('selectedNodes', selectedNodes)
provide('selectedEdges', selectedEdges)
provide('backtestRange', backtestRange)
provide('portfolioConfigs', computed(() => portfolioStore.portfolioConfigs))

// 监听 Vue Flow 的选中状态变化
watch(getSelectedNodes, (newSelectedNodes) => {
  selectedNodes.value = newSelectedNodes
}, { deep: true })

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

// 可视化调试：通知父组件切换视图
const onVisualizeDebug = (debugNodeId) => {
  // 收集当前流程图中所有 DebugNode
  const debugNodes = getNodes.value.filter(n => n.data?.nodeType === 'debug')
  
  // 发送事件到 App.vue
  window.dispatchEvent(new CustomEvent('visualize-strategy', {
    detail: {
      debugNodeId,
      debugNodes,
      nodes: JSON.parse(JSON.stringify(getNodes.value)),
      edges: JSON.parse(JSON.stringify(getEdges.value)),
      strategyId: currentStrategyId.value,
      versionId: currentVersionId.value,
    }
  }))
}

// 监听选项卡切换
watch(activeTab, async (newTab) => {
  if (newTab !== 'backtest') {
    onHideReportConfig()
  }

  if (newTab === 'backtest' && reportViewRef.value) {
    onShowReportConfig()

    await nextTick()
    await new Promise(resolve => setTimeout(resolve, 300))

    const signalNode = getNodes.value.find(node => node.data.nodeType === 'signal')
    const executionNode = getNodes.value.find(node => node.data.nodeType === 'execution')
    if (signalNode && executionNode) {
      // 兼容中文和英文键名
      const codes = signalNode.data.params['代码']?.value || signalNode.data.params['code']?.value
      const rangeDate = executionNode.data.params['回测周期']?.value || executionNode.data.params['range']?.value

      if (codes && rangeDate && rangeDate.length === 2) {
        const symbols = Array.isArray(codes) ? codes : codes.split(',').map(s => s.trim()).filter(s => s.length > 0)
        if (symbols.length > 0 && reportViewRef.value.updatePrice) {
          console.info(`[StrategyFactory] 更新价格图表：标的 ${symbols.join(', ')}, 默认加载 ${symbols[0]}, ${rangeDate[0]} - ${rangeDate[1]}`)
          // 设置所有标的到 select 选项
          if (reportViewRef.value.setSelectedSymbol) {
            reportViewRef.value.setSelectedSymbol(symbols)
          }
          // 只加载第一个标的的价格
          reportViewRef.value.updatePrice(symbols[0], rangeDate[0], rangeDate[1])
        }
      }
    }
  }
})

// 键盘事件
const onKeyDown = (event) => {
  // Ctrl+S / Cmd+S: 保存
  if ((event.ctrlKey || event.metaKey) && event.key === 's' && !event.shiftKey) {
    event.preventDefault()
    handleSaveFlow()
    return
  }
  
  // Ctrl+Shift+S / Cmd+Shift+S: 另存为
  if ((event.ctrlKey || event.metaKey) && event.key === 's' && event.shiftKey) {
    event.preventDefault()
    handleSaveAsNewVersion()
    return
  }
  
  // Delete/Backspace: 删除选中节点或边
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

// 关闭右键菜单
const closeContextMenu = () => {
  contextMenu.value.visible = false
}

// 事件处理包装函数
const handleSaveFlow = () => saveFlow(promptDialogRef.value, infoPanelRef.value?.addInfoMessage)
const handleSaveAsNewVersion = () => saveAsNewVersion(promptDialogRef.value, infoPanelRef.value?.addInfoMessage)

const onSelectionDragStart = () => {}
const onSelectionDrag = () => {}
const onSelectionDragStop = () => {}

const onEdgesDelete = (deletedEdges) => {}

// 生命周期
onMounted(async () => {
  await historyStore.initialize()
  document.addEventListener('click', closeContextMenu)
  document.addEventListener('keydown', onKeyDown)
  console.log('[StrategyFactory] onMounted: 注册 SSE handlers')
})

onUnmounted(() => {
  document.removeEventListener('click', closeContextMenu)
  document.removeEventListener('keydown', onKeyDown)
  console.log('[StrategyFactory] onUnmounted: 移除 SSE handlers')
})

// 包装 runBacktest 和 loadVersionFromHistory
const handleRunBacktest = async () => {
  return runBacktest(reportViewRef, infoPanelRef.value?.addInfoMessage, message, historyStore)
}

const handleLoadVersionFromHistory = async (version) => {
  return loadVersionFromHistory(version, message, reportViewRef)
}

// 对外暴露
const emit = defineEmits(['load-version'])

/**
 * 如果当前画布显示的正是被删除的策略，则清空
 */
const clearCanvasIfStrategyMatches = (strategyId) => {
  if (currentStrategyId.value === strategyId) {
    console.info(`[StrategyFactory] 策略 ${strategyId} 已删除，清空画布`)
    removeNodes(getNodes.value.map(n => n.id))
    removeEdges(getEdges.value.map(e => e.id))
    currentStrategyId.value = null
    currentVersionId.value = null
  }
}

/**
 * 如果当前画布显示的正是被删除的版本，则清空
 */
const clearCanvasIfVersionMatches = (versionId) => {
  if (currentVersionId.value === versionId) {
    console.info(`[StrategyFactory] 版本 ${versionId} 已删除，清空画布`)
    removeNodes(getNodes.value.map(n => n.id))
    removeEdges(getEdges.value.map(e => e.id))
    currentStrategyId.value = null
    currentVersionId.value = null
  }
}

/**
 * 获取当前策略图数据（用于部署到服务端）
 * 返回 JSON 对象（非字符串），供 axios 自动序列化
 */
const getStrategyGraph = () => {
  // 获取策略名称
  const strategyName = currentStrategyId.value
    ? strategies.value.find(s => s.id === currentStrategyId.value)?.name || ''
    : ''

  if (!strategyName) {
    console.warn('[StrategyFactory] 未找到策略名称，无法获取图数据')
    return null
  }

  // 构建图数据（与 runBacktest 中的结构一致）
  const curGraph = {
    version: 1,
    id: `strategy_${currentStrategyId.value}`,
    name: strategyName,
    description: '用户自定义策略',
    backtest: backtestRange.value ? {
      start: backtestRange.value[0],
      end: backtestRange.value[1]
    } : undefined,
    nodes: getNodes.value,
    edges: getEdges.value
  }

  // 先序列化为 JSON 字符串，转换中文键名为英文键名，再解析回对象
  const graphJson = JSON.stringify(curGraph)
  const convertedJson = convertLabelsToKeys(graphJson)
  return JSON.parse(convertedJson)
}

// 对外暴露
defineExpose({
  runBacktest: handleRunBacktest,
  loadVersionFromHistory: handleLoadVersionFromHistory,
  clearCanvasIfStrategyMatches,
  clearCanvasIfVersionMatches,
  getStrategyGraph,
  onVisualizeDebug,
})
</script>

<style scoped>
/* 回测配置栏 */
.backtest-config-bar {
  display: flex;
  align-items: center;
  gap: 12px;
  padding: 8px 16px;
  background: var(--panel-bg);
  border-bottom: 1px solid var(--border-color);
  min-height: 44px;
}

.backtest-config-bar .config-label {
  font-size: 13px;
  color: var(--text-secondary);
  white-space: nowrap;
  margin: 0;
}

.main-container {
  height: 100%;
  display: flex;
  flex-direction: column;
  background-color: var(--dark-bg);
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

/* 回测结果面板 */
.backtest-panel {
  flex: 1;
  display: flex;
  flex-direction: column;
  width: 100%;
  min-height: 0;
}

/* 响应式布局 */
@media (max-width: 1200px) {
  .metrics-grid {
    grid-template-columns: repeat(2, 1fr);
  }
}

@media (max-width: 768px) {
  .tabs {
    padding: 0 10px;
  }

  .tab-button {
    padding: 10px 16px;
    font-size: 14px;
  }
}
</style>
