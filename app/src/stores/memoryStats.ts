/**
 * 记忆模块的开关、阈值与运行指标（设置页与对话流程共用）
 *
 * 开关默认关闭：关闭时对话流程完全不接触记忆存储，既不召回也不写库。
 * 阈值决定「未固化对话的正文 token 超过多少时触发一次记忆更新」。
 */

import { defineStore } from 'pinia'
import { computed, ref } from 'vue'
import { MEMORY_UPDATE_TOKEN_THRESHOLD } from '@/lib/memory/memorySession'
import { snapshot, type MemoryMetricsSnapshot } from '@/lib/memory/memoryMetrics'
import { retainHealth } from '@/lib/memory/retainQueue'

/** 开关的 localStorage key */
const ENABLED_KEY = 'quasarx::memory-enabled'
/** 阈值的 localStorage key */
const THRESHOLD_KEY = 'quasarx::memory-threshold'

export const useMemoryStatsStore = defineStore('memoryStats', () => {
  /** 是否启用记忆；默认关闭 */
  const enabled = ref(false)
  /** 记忆更新阈值（token）；默认用记忆模块的常量 */
  const threshold = ref(MEMORY_UPDATE_TOKEN_THRESHOLD)

  /** 指标刷新计数：指标是模块级纯内存计数，靠它触发重算 */
  const revision = ref(0)

  /** 读取开关；localStorage 不可用时按关闭处理 */
  function readEnabled(): boolean {
    try {
      if (typeof localStorage === 'undefined') return false
      return localStorage.getItem(ENABLED_KEY) === 'true'
    } catch {
      return false
    }
  }

  /** 读取阈值；缺省或非法时回落到默认值 */
  function readThreshold(): number {
    try {
      if (typeof localStorage === 'undefined') return MEMORY_UPDATE_TOKEN_THRESHOLD
      const raw = localStorage.getItem(THRESHOLD_KEY)
      if (!raw) return MEMORY_UPDATE_TOKEN_THRESHOLD
      const parsed = Number(raw)
      if (!Number.isFinite(parsed) || parsed <= 0) return MEMORY_UPDATE_TOKEN_THRESHOLD
      return Math.floor(parsed)
    } catch {
      return MEMORY_UPDATE_TOKEN_THRESHOLD
    }
  }

  /** 从 localStorage 重新读取 */
  function load() {
    enabled.value = readEnabled()
    threshold.value = readThreshold()
    revision.value += 1
  }

  load()

  /** 记忆模块的运行指标 */
  const metrics = computed<MemoryMetricsSnapshot>(() => {
    void revision.value
    const snap = snapshot()
    // 熔断状态在记忆模块里单独维护，这里取真实值覆盖占位
    return { ...snap, breakerState: retainHealth().breakerState }
  })

  /** 开关记忆；关闭时对话流程不再接触记忆存储 */
  function setEnabled(v: boolean) {
    enabled.value = !!v
    try {
      localStorage.setItem(ENABLED_KEY, String(enabled.value))
    } catch {
      /* 隐私模式 / 配额满：忽略，内存里的状态仍然生效 */
    }
  }

  /** 设置记忆更新阈值（token） */
  function setThreshold(n: number) {
    const parsed = Number(n)
    if (!Number.isFinite(parsed) || parsed <= 0) return
    threshold.value = Math.floor(parsed)
    try {
      localStorage.setItem(THRESHOLD_KEY, String(threshold.value))
    } catch {
      /* 忽略 */
    }
  }

  /** 重新读取配置并刷新指标 */
  function reload() {
    load()
  }

  return {
    enabled,
    threshold,
    metrics,
    setEnabled,
    setThreshold,
    reload,
  }
})
