// src/components/flow/composables/useNodeEditing.ts
// 节点标题编辑逻辑

import { ref, nextTick } from 'vue'

export function useNodeEditing(nodeLabel: string) {
  const isEditing = ref(false)
  const editingLabel = ref('')
  const titleInput = ref<HTMLInputElement | null>(null)

  function startEditing() {
    isEditing.value = true
    editingLabel.value = nodeLabel

    nextTick(() => {
      if (titleInput.value) {
        titleInput.value.focus()
        titleInput.value.select()
      }
    })
  }

  function saveEditing(): string | null {
    const label = editingLabel.value.trim()
    if (label !== '') {
      isEditing.value = false
      return label
    }
    cancelEditing()
    return null
  }

  function cancelEditing() {
    isEditing.value = false
    editingLabel.value = nodeLabel
  }

  function handleKeydown(event: KeyboardEvent) {
    event.stopPropagation()
    if (event.key === 'Backspace' || event.key === 'Delete') {
      event.stopImmediatePropagation()
    }
  }

  return {
    isEditing,
    editingLabel,
    titleInput,
    startEditing,
    saveEditing,
    cancelEditing,
    handleKeydown
  }
}
