import { ref, computed } from 'vue'
import axios from 'axios'

// ─── 时间范围预设 (与波动率面板一致) ───

function formatDate(d: Date): string {
  const y = d.getFullYear()
  const m = String(d.getMonth() + 1).padStart(2, '0')
  const day = String(d.getDate()).padStart(2, '0')
  return `${y}-${m}-${day}`
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
  }],
]

// ─── 类型定义 ───

export interface ADFResult {
  statistic: number
  p_value: number
  cv_1pct: number
  cv_5pct: number
  cv_10pct: number
  lags: number
  is_stationary: boolean
}

export interface KPSSResult {
  statistic: number
  p_value: number
  lags: number
  lr_variance: number
  is_stationary: boolean
}

export interface OUProcessResult {
  theta: number
  mu: number
  sigma: number
  half_life: number
  log_likelihood: number
  aic: number
  se_theta: number
  se_mu: number
  se_sigma: number
}

export interface EGFullResult {
  symbol_x: string
  symbol_y: string
  alpha: number
  beta: number
  r_squared: number
  adf: ADFResult
  kpss: KPSSResult
  half_life: number
  is_cointegrated: boolean
  ou: OUProcessResult
  residuals: number[]
}

export interface JohansenResult {
  n_variables: number
  rank: number
  trace_stats: number[]
  trace_cv_95: number[]
  trace_cv_99: number[]
  trace_significant: boolean[]
  max_eigen_stats: number[]
  max_eigen_cv_95: number[]
  max_eigen_cv_99: number[]
  max_eigen_significant: boolean[]
  eigenvectors: number[][]
}

export interface GrangerPairResult {
  from: string
  to?: string
  f_statistic?: number
  wald_stat?: number
  p_value: number
  is_significant: boolean
  optimal_lag: number
  condition_set?: string[]
}

export interface UnitRootResult {
  adf: ADFResult
  kpss: KPSSResult
}

export interface CointegrationResponse {
  symbols: string[]
  dates: string[]
  unit_root: Record<string, UnitRootResult>
  pairwise_eg: EGFullResult[]
  johansen?: JohansenResult
  granger: {
    pairwise: GrangerPairResult[]
    multivariate?: GrangerPairResult[]
  }
}

// ─── Composable ───

export function useCointegrationState() {
  const symbols = ref<string[]>([])
  const dateRange = ref<[string, string] | null>(null)
  const quickRange = ref('近1年')
  const maxLag = ref(10)
  const loading = ref(false)
  const error = ref('')
  const result = ref<CointegrationResponse | null>(null)

  const hasJohansen = computed(() => !!result.value?.johansen)
  const hasMultivariateGranger = computed(() =>
    !!result.value?.granger?.multivariate?.length
  )

  function setQuickRange(range: string) {
    quickRange.value = range
    const found = QUICK_RANGES.find(([label]) => label === range)
    if (found) {
      dateRange.value = found[1]()
    }
  }

  function updateDateRange(value: string, type: 'start' | 'end') {
    if (!dateRange.value) return
    dateRange.value = type === 'start'
      ? [value, dateRange.value[1]]
      : [dateRange.value[0], value]
  }

  // 初始化默认时间范围
  setQuickRange('近1年')

  async function analyze() {
    if (symbols.value.length < 2) {
      error.value = '至少选择 2 个标的'
      return
    }
    loading.value = true
    error.value = ''
    result.value = null

    try {
      const params: Record<string, string> = {
        symbols: symbols.value.join(','),
        max_lag: String(maxLag.value),
      }
      if (dateRange.value) {
        if (dateRange.value[0]) params.start_date = dateRange.value[0]
        if (dateRange.value[1]) params.end_date = dateRange.value[1]
      }

      const { data } = await axios.get('/v0/analysis/cointegration', { params })
      result.value = data
    } catch (e: any) {
      error.value = e.response?.data?.error || e.message || '请求失败'
    } finally {
      loading.value = false
    }
  }

  function reset() {
    result.value = null
    error.value = ''
  }

  return {
    QUICK_RANGES,
    symbols,
    dateRange,
    quickRange,
    setQuickRange,
    updateDateRange,
    maxLag,
    loading,
    error,
    result,
    hasJohansen,
    hasMultivariateGranger,
    analyze,
    reset,
  }
}
