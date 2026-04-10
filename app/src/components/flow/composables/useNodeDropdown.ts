// src/components/flow/composables/useNodeDropdown.ts
// 下拉框状态管理

import { ref } from 'vue'

export function useNodeDropdown() {
  const openDropdowns = ref<Record<string, boolean>>({})

  function toggleDropdown(paramKey: string) {
    const newState = { ...openDropdowns.value }
    Object.keys(newState).forEach(key => {
      if (key !== paramKey) {
        newState[key] = false
      }
    })
    newState[paramKey] = !newState[paramKey]
    openDropdowns.value = newState
  }

  function closeAllDropdowns() {
    openDropdowns.value = {}
  }

  function isDropdownOpen(paramKey: string): boolean {
    return !!openDropdowns.value[paramKey]
  }

  return {
    openDropdowns,
    toggleDropdown,
    closeAllDropdowns,
    isDropdownOpen
  }
}
