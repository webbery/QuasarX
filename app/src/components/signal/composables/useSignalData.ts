// app/src/components/signal/composables/useSignalData.ts
// 信号分析数据获取

import { ref } from 'vue'
import axios from 'axios'
import { ElMessage } from 'element-plus'
import type { SignalAnalysisResult } from './useSignalState'

/** 判断 symbol 是否为宏观指标 (格式: country/indicator) */
function isMacroSymbol(symbol: string): boolean {
  const parts = symbol.split('/')
  return parts.length === 2
}

export function useSignalData() {
  const loading = ref(false)

  async function fetchSignal(
    symbols: string[],
    startDate: string,
    endDate: string,
    field: string = 'close',
    method: string = 'emd',
    numImfs: number = 5,
    fillMethod: string = 'none',
    rollingWindow: number = 60
  ): Promise<SignalAnalysisResult | null> {
    if (symbols.length === 0) {
      ElMessage.warning('请至少添加一个标的')
      return null
    }

    loading.value = true
    try {
      const macroSymbols = symbols.filter(isMacroSymbol)
      const isMacroMode = macroSymbols.length > 0

      const params: Record<string, any> = {
        symbols: symbols.join(','),
        start_date: startDate,
        end_date: endDate,
        field,
        method,
        num_imfs: numImfs,
        fill_method: fillMethod,
        rolling_window: rollingWindow
      }

      if (isMacroMode) {
        const [country, indicator] = macroSymbols[0].split('/')
        params.country = country
        params.indicator = indicator
      }

      const response = await axios.get('/v0/analysis/signal', { params })

      return response.data as SignalAnalysisResult
    } catch (err: any) {
      const msg = err.response?.data?.error || err.message || '请求失败'
      ElMessage.error(`信号分析失败: ${msg}`)
      console.error('[SignalData]', err)
      return null
    } finally {
      loading.value = false
    }
  }

  return {
    loading,
    fetchSignal
  }
}
