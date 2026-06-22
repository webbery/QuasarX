// app/src/components/volatility/composables/useVolatilityState.ts
// 波动率分析状态管理

import { ref, reactive } from 'vue'

export interface SymbolItem {
  symbol: string
  label?: string
}

export interface VolatilityState {
  /** 选中的标的列表 */
  symbols: SymbolItem[]
  /** 当前编辑的标的 */
  editingSymbol: string
  /** 分析字段 (close/open/high/low/volume) */
  field: string
  /** 时间范围 */
  dateRange: [string, string] | null
  /** 快速时间选择 */
  quickRange: string
  /** 滚动窗口配置 */
  windows: number[]
  /** 是否正在加载 */
  loading: boolean
  /** 分析结果 */
  result: VolatilityAnalysisResult | null
}

export interface VolatilityAnalysisResult {
  symbols: string[]
  dates: string[]
  single: Record<string, VolatilitySingleResult>
  multi: VolatilityMultiResult
}

export interface VolatilitySingleResult {
  prices: number[]
  returns: number[]
  volumes: number[]
  abs_returns: number[]
  rolling_vol: Record<string, number[]>
  upper_2sigma: number[]
  upper_1sigma: number[]
  mean_price: number[]
  lower_1sigma: number[]
  lower_2sigma: number[]
  annual_volatility: number
  max_drawdown: number
  skewness: number
  kurtosis: number
  var_95: number
  cvar_95: number
  returns_acf: number[]
  returns_pacf: number[]
  abs_returns_acf: number[]
  acf_decay: {
    lb_statistic: number
    lb_pvalue: number
    has_autocorrelation: boolean
    exponential_r2: number
    hyperbolic_r2: number
    decay_mode: 'exponential' | 'hyperbolic' | 'inconclusive'
    decay_half_life: number
    hurst_estimate: number
  }
  // AR(p) 预测结果
  forecast_returns: ForecastResult | null
  forecast_vol: ForecastResult | null
}

export interface ForecastResult {
  source_series: string
  order_p: number
  ar_coeffs: number[]
  residual_var: number
  forecast_values: number[]
  forecast_upper_1sigma: number[]
  forecast_lower_1sigma: number[]
  forecast_upper_2sigma: number[]
  forecast_lower_2sigma: number[]
  forecast_std: number[]
  has_autocorrelation: boolean
  note: string
}

export interface VolatilityMultiResult {
  correlation_matrix: number[][]
  covariance_matrix: number[][]
  eigenvalues: number[]
  condition_number: number
  is_positive_definite: boolean
  annual_volatility: number[]
  // 多资产预测外推
  multi_forecast: MultiForecast | null
}

export interface MultiForecast {
  horizon: number
  symbols: string[]
  forecast_cov: number[][]
  forecast_corr: number[][]
  forecast_volatilities: number[]
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

export function useVolatilityState() {
  const state = reactive<VolatilityState>({
    symbols: [],
    editingSymbol: '',
    field: 'close',
    dateRange: null,
    quickRange: '近1年',
    windows: [20, 60, 120],
    loading: false,
    result: null
  })

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
    QUICK_RANGES,
    addSymbol,
    removeSymbol,
    setQuickRange
  }
}
