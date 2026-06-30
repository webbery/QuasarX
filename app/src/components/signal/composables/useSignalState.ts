// app/src/components/signal/composables/useSignalState.ts
// 信号分析状态管理

import { reactive, shallowRef } from 'vue'

export interface SymbolItem {
  symbol: string
  label?: string
}

export interface SignalState {
  symbols: SymbolItem[]
  editingSymbol: string
  field: string
  method: string
  numImfs: number
  dateRange: [string, string] | null
  quickRange: string
  /** 数据频率 (1m/5m/15m/30m/1h/4h/1d/1w/1M) */
  frequency: string
  loading: boolean
}

export interface SignalAnalysisResult {
  symbols: string[]
  field: string
  method: string
  dates: string[]
  original: number[]
  imf_components: number[][]
  residual: number[]
  imf_info: { index: number; mean_period: number; energy_pct: number }[]
  reconstruction_error: number
}

const QUICK_RANGES: [string, () => [string, string]][] = [
  ['近1月', () => {
    const end = new Date()
    const start = new Date()
    start.setMonth(start.getMonth() - 1)
    return [formatDate(start), formatDate(end)]
  }],
  ['近3月', () => {
    const end = new Date()
    const start = new Date()
    start.setMonth(start.getMonth() - 3)
    return [formatDate(start), formatDate(end)]
  }],
  ['近6月', () => {
    const end = new Date()
    const start = new Date()
    start.setMonth(start.getMonth() - 6)
    return [formatDate(start), formatDate(end)]
  }],
  ['近1年', () => {
    const end = new Date()
    const start = new Date()
    start.setFullYear(start.getFullYear() - 1)
    return [formatDate(start), formatDate(end)]
  }],
  ['近3年', () => {
    const end = new Date()
    const start = new Date()
    start.setFullYear(start.getFullYear() - 3)
    return [formatDate(start), formatDate(end)]
  }]
]

function formatDate(d: Date): string {
  const y = d.getFullYear()
  const m = String(d.getMonth() + 1).padStart(2, '0')
  const day = String(d.getDate()).padStart(2, '0')
  return `${y}-${m}-${day}`
}

export function useSignalState() {
  const state = reactive<Omit<SignalState, 'result'>>({
    symbols: [],
    editingSymbol: '',
    field: 'close',
    method: 'emd',
    numImfs: 5,
    dateRange: null,
    quickRange: '近1年',
    frequency: '1d',
    loading: false
  })

  const result = shallowRef<SignalAnalysisResult | null>(null)

  function addSymbol(symbol: string) {
    if (!state.symbols.find(s => s.symbol === symbol)) {
      state.symbols.push({ symbol })
    }
  }

  function removeSymbol(index: number) {
    state.symbols.splice(index, 1)
  }

  function setQuickRange(range: string) {
    state.quickRange = range
    const found = QUICK_RANGES.find(([label]) => label === range)
    if (found) {
      state.dateRange = found[1]()
    }
  }

  // 初始化默认时间范围
  setQuickRange('近1年')

  return {
    state,
    result,
    resultRef: result,
    QUICK_RANGES,
    addSymbol,
    removeSymbol,
    setQuickRange,
    formatDate
  }
}
