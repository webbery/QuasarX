// app/src/components/pca/composables/usePCAData.ts

import { ref } from 'vue'
import axios from 'axios'
import type { PCAResult } from './usePCAState'

export function usePCAData() {
  const loading = ref(false)
  const error = ref<string | null>(null)

  async function fetchPCA(
    symbols: string[],
    dateRange: [string, string] | null,
    field: string,
    nComponents: number,
    mode: 'cross_section' | 'time_series',
    fillMethod: string
  ): Promise<PCAResult | null> {
    if (symbols.length === 0) {
      error.value = '请至少选择一个标的'
      return null
    }

    loading.value = true
    error.value = null

    try {
      const params: Record<string, any> = {
        symbols: symbols.join(','),
        field,
        n_components: nComponents,
        mode: mode === 'time_series' ? 'timeseries' : 'cross_section',
        fill_method: fillMethod
      }

      if (dateRange) {
        params.start_date = dateRange[0]
        params.end_date = dateRange[1]
      }

      const response = await axios.get('/v0/analysis/pca', { params })
      return response.data as PCAResult
    } catch (e: any) {
      error.value = e.response?.data?.error || e.message || '请求失败'
      return null
    } finally {
      loading.value = false
    }
  }

  return {
    loading,
    error,
    fetchPCA
  }
}
