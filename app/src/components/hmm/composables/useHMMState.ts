import { ref, shallowRef, computed } from 'vue'

export interface HMMConfig {
  n_states: number
  max_iter: number
  tol: number
  regularization: number
  features: string[]
}

// 与后端 HMMHandler.cpp 的 kKnownFeatures 一一对应，改这里必须同步改那里，
// 否则请求会以 400 unknown feature 被拒。
export const HMM_FEATURE_OPTIONS = [
  { value: 'return_1d', label: '1日收益率' },
  { value: 'log_return', label: '对数收益率' },
  { value: 'volatility_20d', label: '20日波动率' },
  { value: 'volume_ratio', label: '量比' },
]

export interface HMMResult {
  states: number[]
  probs: number[][]
  transition: number[][]
  duration: number[]
  log_likelihood: number[]
  state_means: number[][]
  state_covs: number[][]
  dates: string[]
  // 观测矩阵的列名（"sh.600111.return_1d"），顺序与 state_means 的列一一对应。
  // 多标的时列数 = 标的数 × 特征数，雷达图的轴必须用它而不是请求时的裸特征名。
  feature_names: string[]
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

export function useHMMState() {
  const config = ref<HMMConfig>({
    n_states: 3,
    max_iter: 100,
    tol: 1e-4,
    regularization: 1e-6,
    features: ['return_1d']
  })

  const result = shallowRef<HMMResult | null>(null)
  const loading = ref(false)
  const error = ref<string | null>(null)

  const selectedStrategyId = ref('')
  const quickRange = ref('近1年')
  const dateRange = ref<[string, string] | null>(null)
  const frequency = ref('1d')

  function setQuickRange(range: string) {
    quickRange.value = range
    const found = QUICK_RANGES.find(([label]) => label === range)
    if (found) dateRange.value = found[1]()
  }

  // 特征选择器是单选（对齐 ML 面板的「字段:」下拉框），但接口要的是数组，
  // 故 config.features 始终保持单元素数组。
  const feature = computed(() => config.value.features[0] ?? '')

  function setFeature(value: string) {
    config.value.features = [value]
  }

  // 初始化默认日期范围
  setQuickRange('近1年')

  function reset() {
    result.value = null
    error.value = null
    loading.value = false
  }

  return {
    config,
    result,
    loading,
    error,
    selectedStrategyId,
    quickRange,
    dateRange,
    frequency,
    feature,
    QUICK_RANGES,
    setQuickRange,
    setFeature,
    reset
  }
}
