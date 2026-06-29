// app/src/components/pca/composables/usePCAState.ts

import { reactive, shallowRef, ref } from 'vue'

export interface PCAQualityMetrics {
  kmo: number
  kmo_grade: string
  bartlett_stat: number
  bartlett_pvalue: number
  cumulative_variance: number
  variance_grade: string
  condition_number: number
  cond_grade: string
  is_positive_definite: boolean
  reconstruction_error: number
  reconstruction_error_pct: number
}

export interface PCACrossSectionResult {
  symbols: string[]
  eigenvalues: number[]
  variance_ratio: number[]
  cumulative_variance: number[]
  loadings: number[][]
  scores: number[][]
  corr_original: number[][]
  corr_reconstructed: number[][]
  n_components: number
  n_symbols: number
  n_observations: number
  quality: PCAQualityMetrics
}

export interface PCATimeSeriesResult {
  symbol: string
  features: string[]
  eigenvalues: number[]
  variance_ratio: number[]
  cumulative_variance: number[]
  loadings: number[][]
  scores: number[][]
  n_components: number
  n_features: number
  n_observations: number
  quality: PCAQualityMetrics
}

export interface PCAResult {
  mode: 'cross_section' | 'time_series'
  dates: string[]
  cross_section?: PCACrossSectionResult
  time_series?: PCATimeSeriesResult
}

export interface PCAState {
  mode: 'cross_section' | 'time_series'
  symbols: string[]
  editingSymbol: string
  field: string
  dateRange: [string, string] | null
  quickRange: string
  nComponents: number  // 0 = auto
  fillMethod: string
  loading: boolean
}

const QUICK_RANGES: [string, () => [string, string]][] = [
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

export function usePCAState() {
  const state = reactive<Omit<PCAState, 'result'>>({
    mode: 'cross_section',
    symbols: [],
    editingSymbol: '',
    field: 'return',
    dateRange: null,
    quickRange: '近1年',
    nComponents: 0,
    fillMethod: 'Forward',
    loading: false
  })

  const result = shallowRef<PCAResult | null>(null)
  const selectedK = ref(0)  // 用户选择保留的 PC 数量

  function addSymbol(symbol: string) {
    if (!state.symbols.includes(symbol)) {
      state.symbols.push(symbol)
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
    selectedK,
    QUICK_RANGES,
    addSymbol,
    removeSymbol,
    setQuickRange,
    formatDate
  }
}
