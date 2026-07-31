// app/src/components/ml/composables/useMLState.ts
// ML 训练与分析面板状态管理

import { reactive, ref } from 'vue'

export interface TrainStep {
  id: string
  label: string
  status: 'pending' | 'running' | 'done' | 'error'
  elapsedMs?: number
  detail?: string
}

export interface TrainLog {
  step: string
  level: 'info' | 'warning' | 'error'
  line: string
}

export interface LearningCurvePoint {
  iteration: number
  train_loss: number
  eval_loss: number
}

export interface FeatureImportance {
  feature: string
  gain: number
  weight: number
  cover: number
}

export interface Prediction {
  actual: number
  predicted: number
  pred_class: number
}

export interface TrainResult {
  model_id: number
  model_path: string
  best_iteration: number
  n_train: number
  n_test: number
  n_features: number
  features: string[]
  learning_curve: LearningCurvePoint[]
  feature_importance: FeatureImportance[]
  eval_metrics: Record<string, number>
  predictions: Prediction[]
}

export interface ShapResult {
  model_id: number
  features: string[]
  shap: number[][]
  base_value: number[]
  n_samples: number
}

export interface LabelAnalysisResult {
  symbol: string
  field: string
  dates: string[]
  prices: number[]
  labels: number[]       // 0=UP, 1=FLAT, 2=DOWN
  threshold: number
}

export interface BatchLabelStat {
  symbol: string
  total: number
  up: number
  flat: number
  down: number
  upPct: number
  flatPct: number
  downPct: number
  threshold: number
}

export interface FeatureStat {
  name: string
  valid: number
  nan_count: number
  nan_pct: number
  min: number | null
  max: number | null
  mean: number | null
  std: number | null
  median: number | null
}

export interface FeatureReport {
  total_rows: number
  n_features: number
  date_start: string
  date_end: string
  csv_path: string
  features: FeatureStat[]
}

export const LABEL_TYPES = [
  { label: '自适应分类（涨/平/跌）', value: 'classification' },
  { label: '回归（收益率）', value: 'regression' },
]

export const REG_MODES = [
  { label: 'L1 (Lasso)', value: 'l1' },
  { label: 'L2 (Ridge)', value: 'l2' },
  { label: 'L1 + L2', value: 'both' },
  { label: '无', value: 'none' },
]

const QUICK_RANGES: [string, () => [string, string]][] = [
  ['近1月', () => {
    const end = new Date(); const start = new Date()
    start.setMonth(start.getMonth() - 1)
    return [start.toISOString().slice(0, 10), end.toISOString().slice(0, 10)]
  }],
  ['近3月', () => {
    const end = new Date(); const start = new Date()
    start.setMonth(start.getMonth() - 3)
    return [start.toISOString().slice(0, 10), end.toISOString().slice(0, 10)]
  }],
  ['近6月', () => {
    const end = new Date(); const start = new Date()
    start.setMonth(start.getMonth() - 6)
    return [start.toISOString().slice(0, 10), end.toISOString().slice(0, 10)]
  }],
  ['近1年', () => {
    const end = new Date(); const start = new Date()
    start.setFullYear(start.getFullYear() - 1)
    return [start.toISOString().slice(0, 10), end.toISOString().slice(0, 10)]
  }],
  ['近3年', () => {
    const end = new Date(); const start = new Date()
    start.setFullYear(start.getFullYear() - 3)
    return [start.toISOString().slice(0, 10), end.toISOString().slice(0, 10)]
  }],
]

export function useMLState() {
  const selectedStrategyId = ref<string>('')
  const field = ref('close')
  const quickRange = ref('近1年')
  const dateRange = ref<[string, string] | null>(null)
  const frequency = ref('1d')
  const labelSymbol = ref('')
  const labelAnalysis = reactive<{
    result: LabelAnalysisResult | null
    loading: boolean
  }>({
    result: null,
    loading: false,
  })

  // 价格缓存（用于滑块拖动时快速重算标签，避免重新请求网络）
  const priceCache = reactive<{
    dates: string[]
    prices: number[]
    symbol: string
    field: string
  }>({
    dates: [],
    prices: [],
    symbol: '',
    field: '',
  })

  // 批量标签分析
  const batchAnalysis = reactive<{
    results: BatchLabelStat[]
    loading: boolean
    progress: string
  }>({
    results: [],
    loading: false,
    progress: '',
  })

  const trainResult = reactive<{
    data: TrainResult | null
    shap: ShapResult | null
    loading: boolean
    progressMsg: string
    steps: TrainStep[]
    logs: TrainLog[]
  }>({
    data: null,
    shap: null,
    loading: false,
    progressMsg: '',
    steps: [],
    logs: [],
  })

  const featureReport = reactive<{
    data: FeatureReport | null
    loading: boolean
    steps: TrainStep[]
    logs: TrainLog[]
  }>({
    data: null,
    loading: false,
    steps: [],
    logs: [],
  })

  // 训练配置
  const config = reactive({
    labelSource: '',         // 标签来源变量（如 "sh.600000.close"）
    labelPeriod: 5,          // 未来周期
    labelType: 'classification',
    volK: 0.5,               // 自适应阈值系数：threshold = volK × σ × √N
    objective: 'multi:softprob',
    numClass: 3,
    testRatio: 0.2,
    learningRate: 0.1,
    maxDepth: 6,
    nEstimators: 200,
    earlyStoppingRounds: 20,
    // 正则化与采样参数
    subsample: 0.8,          // 行采样比例
    colsampleBytree: 0.8,    // 列采样比例
    regMode: 'both',         // 正则化模式：none / l1 / l2 / both
    regValue: 1.0,           // 正则化强度
    gamma: 0.0,              // 分裂最小增益
    minChildWeight: 1,       // 叶节点最小样本权重
    scalePosWeight: 1.0,     // 正样本权重（类别不平衡时使用）
  })

  function setFrequency(f: string) {
    frequency.value = f
  }

  function setField(f: string) {
    field.value = f
  }

  function setQuickRange(range: string) {
    quickRange.value = range
    const found = QUICK_RANGES.find(([label]) => label === range)
    if (found) dateRange.value = found[1]()
  }

  // 初始化默认日期范围
  setQuickRange('近1年')

  function reset() {
    trainResult.data = null
    trainResult.shap = null
    trainResult.progressMsg = ''
    trainResult.loading = false
    trainResult.steps = []
    trainResult.logs = []
    featureReport.data = null
    featureReport.loading = false
    featureReport.steps = []
    featureReport.logs = []
    labelAnalysis.result = null
    labelAnalysis.loading = false
    labelSymbol.value = ''
    priceCache.dates = []
    priceCache.prices = []
    priceCache.symbol = ''
    priceCache.field = ''
    batchAnalysis.results = []
    batchAnalysis.loading = false
    batchAnalysis.progress = ''
  }

  /** 从缓存的价格数据重新计算标签（滑块拖动时调用，无需网络请求） */
  function recomputeLabels() {
    if (priceCache.dates.length === 0) return

    const prices = priceCache.prices
    const n = prices.length
    const period = config.labelPeriod
    const volK = config.volK
    const labelType = config.labelType
    const labels = new Array<number>(n).fill(-1)
    let threshold = 0.015

    if (labelType === 'classification') {
      const logRets: number[] = []
      for (let i = 1; i < n; i++) {
        if (prices[i] > 0 && prices[i - 1] > 0) {
          logRets.push(Math.log(prices[i] / prices[i - 1]))
        }
      }
      if (logRets.length >= 20) {
        const mean = logRets.reduce((a, b) => a + b, 0) / logRets.length
        const variance = logRets.reduce((a, b) => a + (b - mean) ** 2, 0) / logRets.length
        const sigma = Math.sqrt(variance)
        if (sigma >= 1e-12) {
          threshold = volK * sigma * Math.sqrt(period)
          threshold = Math.max(0.005, Math.min(0.10, threshold))
        }
      }
      for (let i = 0; i < n; i++) {
        const futureIdx = i + period
        if (futureIdx < n && prices[i] > 0) {
          const futureRet = prices[futureIdx] / prices[i] - 1
          if (futureRet > threshold) labels[i] = 0
          else if (futureRet < -threshold) labels[i] = 2
          else labels[i] = 1
        }
      }
    } else {
      for (let i = 0; i < n; i++) {
        const futureIdx = i + period
        if (futureIdx < n && prices[i] > 0) {
          labels[i] = prices[futureIdx] / prices[i] - 1
        }
      }
      threshold = volK * 0.01
    }

    labelAnalysis.result = {
      symbol: priceCache.symbol,
      field: priceCache.field,
      dates: [...priceCache.dates],
      prices: [...priceCache.prices],
      labels,
      threshold,
    }
  }

  function isClassification() {
    return config.objective === 'binary:logistic' || config.objective === 'multi:softprob'
  }

  /** 根据 labelType 自动推导 objective */
  function syncObjective() {
    if (config.labelType === 'classification') {
      config.objective = config.numClass === 2 ? 'binary:logistic' : 'multi:softprob'
    } else {
      config.objective = 'reg:squarederror'
    }
  }

  return {
    selectedStrategyId,
    field,
    setField,
    quickRange,
    dateRange,
    frequency,
    setFrequency,
    labelSymbol,
    labelAnalysis,
    priceCache,
    batchAnalysis,
    recomputeLabels,
    trainResult,
    featureReport,
    config,
    QUICK_RANGES,
    setQuickRange,
    reset,
    isClassification,
    syncObjective,
  }
}
