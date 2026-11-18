/**
 * 模型基底状态（设置页与聊天输入框共用）
 */

import { defineStore } from 'pinia'
import { computed, ref } from 'vue'
import {
  isBaseReady,
  loadModelBaseState,
  resetModelBase,
  setActiveModelBase,
  updateModelBase,
  type ModelBaseConfig,
  type ModelBaseId,
} from '@/lib/agent/modelBases'

export const useAgentModelStore = defineStore('agentModel', () => {
  const bases = ref<ModelBaseConfig[]>([])
  const activeId = ref<ModelBaseId>('deepseek')

  /** 从 localStorage 重新读取 */
  function load() {
    const state = loadModelBaseState()
    bases.value = state.bases
    activeId.value = state.activeId
  }

  load()

  const activeBase = computed<ModelBaseConfig | null>(
    () => bases.value.find((base) => base.presetId === activeId.value) ?? bases.value[0] ?? null,
  )

  /** 已配置好的基底（下拉里可选的） */
  const readyBases = computed<ModelBaseConfig[]>(() => bases.value.filter(isBaseReady))

  const activeReady = computed(() => !!activeBase.value && isBaseReady(activeBase.value))

  function setActive(id: ModelBaseId) {
    const state = setActiveModelBase(id)
    bases.value = state.bases
    activeId.value = state.activeId
  }

  function saveBase(config: ModelBaseConfig) {
    const state = updateModelBase(config)
    bases.value = state.bases
    activeId.value = state.activeId
  }

  function resetBase(id: ModelBaseId) {
    const state = resetModelBase(id)
    bases.value = state.bases
    activeId.value = state.activeId
  }

  function reload() {
    load()
  }

  return {
    bases,
    activeId,
    activeBase,
    readyBases,
    activeReady,
    setActive,
    saveBase,
    resetBase,
    reload,
  }
})
