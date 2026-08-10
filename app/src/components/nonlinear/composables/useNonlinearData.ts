import { ref } from 'vue'
import axios from 'axios'
import { ElMessage } from 'element-plus'
import type { NonlinearResult } from './useNonlinearState'

export function useNonlinearData() {
  const loading = ref(false)

  async function fetchNonlinear(
    symbol: string,
    startDate: string,
    endDate: string,
    field: string,
    params: {
      qMin: number; qMax: number; qStep: number; minWindow: number
      embedDim: number; timeDelay: number; lyapunovHorizon: number
    }
  ): Promise<NonlinearResult | null> {
    if (!symbol) {
      ElMessage.warning('请至少添加一个标的')
      return null
    }

    loading.value = true
    try {
      const response = await axios.get('/v0/analysis/nonlinear', {
        params: {
          symbols: symbol,
          start_date: startDate,
          end_date: endDate,
          field,
          q_min: params.qMin,
          q_max: params.qMax,
          q_step: params.qStep,
          min_window: params.minWindow,
          embed_dim: params.embedDim,
          time_delay: params.timeDelay,
          lyapunov_horizon: params.lyapunovHorizon,
        }
      })
      return response.data as NonlinearResult
    } catch (err: any) {
      const msg = err.response?.data?.error || err.message || '请求失败'
      ElMessage.error(`非线性分析失败: ${msg}`)
      return null
    } finally {
      loading.value = false
    }
  }

  return { loading, fetchNonlinear }
}
