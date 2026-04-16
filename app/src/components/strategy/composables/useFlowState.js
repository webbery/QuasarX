import { ref, computed } from 'vue'
import { useVueFlow } from '@vue-flow/core'

/**
 * 流程图状态管理 composable
 * 管理 VueFlow 实例引用、节点/边状态、选中的节点/边、策略版本信息等
 */
export function useFlowState() {
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
    onNodesInitialized
  } = useVueFlow()

  // 节点和边的选中状态
  const selectedNodes = ref([])
  const selectedEdges = ref([])

  // 策略和版本信息
  const currentStrategyId = ref(null)
  const currentVersionId = ref(null)

  // 未保存更改标记
  const hasUnsavedChanges = ref(false)

  // 节点 ID 计数器
  const nodeIdCounter = ref(10)

  // 活动标签
  const activeTab = ref('flow')

  return {
    // VueFlow 方法
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
    onNodesInitialized,

    // 状态
    selectedNodes,
    selectedEdges,
    currentStrategyId,
    currentVersionId,
    hasUnsavedChanges,
    nodeIdCounter,
    activeTab
  }
}
