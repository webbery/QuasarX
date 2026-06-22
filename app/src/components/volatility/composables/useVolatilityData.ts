// app/src/components/volatility/composables/useVolatilityData.ts
// 波动率分析数据获取

import { ref } from 'vue'
import axios from 'axios'
import { ElMessage } from 'element-plus'
import type { VolatilityAnalysisResult } from './useVolatilityState'

/** 判断 symbol 是否为宏观指标 (格式: country/indicator) */
function isMacroSymbol(symbol: string): boolean {
  const parts = symbol.split('/')
  return parts.length === 2
}

export function useVolatilityData() {
  const loading = ref(false)

  async function fetchVolatility(
    symbols: string[],
    startDate: string,
    endDate: string,
    windows: number[] = [20, 60, 120],
    field: string = 'close',
    fillMethod: string = 'none',
    bandWindow: number = 20
  ): Promise<VolatilityAnalysisResult | null> {
    if (symbols.length === 0) {
      ElMessage.warning('请至少添加一个标的')
      return null
    }

    loading.value = true
    try {
      // 检测是否为宏观指标模式
      const macroSymbols = symbols.filter(isMacroSymbol)
      const isMacroMode = macroSymbols.length > 0

      const url = '/v0/analysis/volatility'
      let params: any

      if (isMacroMode) {
        // 宏观指标模式：传递 country 和 indicator 参数
        const [country, indicator] = macroSymbols[0].split('/')
        params = {
          symbols: symbols.join(','),
          start_date: startDate,
          end_date: endDate,
          windows: windows.join(','),
          field,
          fill_method: fillMethod,
          band_window: bandWindow,
          // 宏观指标参数
          country,
          indicator
        }
      } else {
        // 策略行情模式：原有逻辑
        params = {
          symbols: symbols.join(','),
          start_date: startDate,
          end_date: endDate,
          windows: windows.join(','),
          field,
          fill_method: fillMethod,
          band_window: bandWindow
        }
      }

      const response = await axios.get(url, { params })

      return response.data as VolatilityAnalysisResult
    } catch (err: any) {
      const msg = err.response?.data?.error || err.message || '请求失败'
      const status = err.response?.status || 'N/A'
      ElMessage.error(`波动率分析失败: [${status}] ${msg}`)
      console.error('[VolatilityData] 请求失败:', err)
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
