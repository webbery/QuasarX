// app/src/components/volatility/composables/useVolatilityData.ts
// 波动率分析数据获取

import { ref } from 'vue'
import axios from 'axios'
import { ElMessage } from 'element-plus'
import type { VolatilityAnalysisResult } from './useVolatilityState'

export function useVolatilityData() {
  const loading = ref(false)

  async function fetchVolatility(
    symbols: string[],
    startDate: string,
    endDate: string,
    windows: number[] = [20, 60, 120]
  ): Promise<VolatilityAnalysisResult | null> {
    if (symbols.length === 0) {
      ElMessage.warning('请至少添加一个标的')
      return null
    }

    loading.value = true
    try {
      const response = await axios.get('/v0/analysis/volatility', {
        params: {
          symbols: symbols.join(','),
          start_date: startDate,
          end_date: endDate,
          windows: windows.join(',')
        }
      })

      return response.data as VolatilityAnalysisResult
    } catch (err: any) {
      const msg = err.response?.data?.error || err.message || '请求失败'
      ElMessage.error(`波动率分析失败: ${msg}`)
      console.error('[VolatilityData]', err)
      return null
    } finally {
      loading.value = false
    }
  }

  return {
    loading,
    fetchVolatility
  }
}
