/**
 * 策略关联模型清单 Composable
 * 批量拉取策略中所有 XGBoostNode 绑定的模型信息（production + 最新 experiment + is_latest）
 *
 * 数据来源：POST /v0/strategy {action:"batch_models", names:[...]} → StrategyBatchModelsResponse
 * 后端 10s TTL 缓存，前端同样 10s 内复用，避免每个策略行展开都打后端
 */

import { ref, type Ref } from 'vue'
import axios from 'axios'

export interface StrategyModelItem {
  node_id: string
  label: string
  model_file: string
  production: { path: string; meta: Record<string, any> } | null
  latest_experiment: { path: string; meta: Record<string, any> } | null
  is_latest: boolean
}

export interface StrategyModelsResult {
  strategy: string
  error?: string
  models: StrategyModelItem[]
}

export interface StrategyModelsState {
  data: Ref<Map<string, StrategyModelsResult>>
  loading: Ref<boolean>
  error: Ref<string | null>
  lastFetchAt: Ref<number>
}

export function useStrategyModels() {
  const data = ref<Map<string, StrategyModelsResult>>(new Map())
  const loading = ref(false)
  const error = ref<string | null>(null)
  const lastFetchAt = ref(0)

  /**
   * 拉取一批策略的模型清单。
   * @param names 策略名数组（空数组 = 跳过请求）
   * @param force true 时跳过前端 10s 缓存强制重拉
   */
  async function fetchModels(names: string[], force = false): Promise<Map<string, StrategyModelsResult>> {
    if (names.length === 0) return data.value
    const now = Date.now()
    if (!force && (now - lastFetchAt.value) < 10_000 && data.value.size > 0) {
      return data.value
    }
    loading.value = true
    error.value = null
    try {
      const resp = await axios.post('/v0/strategy', {
        action: 'batch_models',
        names,
      })
      const next = new Map<string, StrategyModelsResult>()
      for (const [name, payload] of Object.entries(resp.data ?? {})) {
        next.set(name, payload as StrategyModelsResult)
      }
      data.value = next
      lastFetchAt.value = Date.now()
    } catch (e: any) {
      error.value = e?.response?.data?.message || e?.message || 'fetch failed'
      // 保留旧数据，避免 UI 因网络抖动全空
    } finally {
      loading.value = false
    }
    return data.value
  }

  /** 强制刷新（点 🔄 按钮调用） */
  async function refresh(names: string[]) {
    return fetchModels(names, true)
  }

  return { data, loading, error, lastFetchAt, fetchModels, refresh }
}