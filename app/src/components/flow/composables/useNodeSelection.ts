// src/components/flow/composables/useNodeSelection.ts
// 节点选中状态管理

import { computed, type Ref } from 'vue'

export function useNodeSelection(
  nodeId: string,
  selectedNodes: Ref<any[]>
) {
  const isSelected = computed(() => {
    return selectedNodes?.value?.find(n => n.id === nodeId)
  })

  const isMultiSelected = computed(() => {
    return selectedNodes?.value?.length > 1 &&
           selectedNodes?.value?.find(n => n.id === nodeId)
  })

  const selectionIndex = computed(() => {
    if (!selectedNodes?.value) return 0
    const index = selectedNodes.value.findIndex(n => n.id === nodeId)
    return index >= 0 ? index + 1 : 0
  })

  return {
    isSelected,
    isMultiSelected,
    selectionIndex
  }
}
