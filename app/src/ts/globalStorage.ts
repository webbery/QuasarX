import { defineStore } from 'pinia'
import { ref, markRaw } from 'vue'

export const getGlobalStorage = defineStore('globalStorage', () => {
  // 使用 ref 包装 Map 对象
  const globalInstance = ref<Map<string, any>>(new Map())
  const isLoading = ref(false)

  const setItem = (key: string, value: any) => {
    globalInstance.value.set(key, value)
  }

  const getItem = (key: string): any => {
    return globalInstance.value.get(key)
  }
  return {
    setItem, getItem
  }
})