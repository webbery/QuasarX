// app/src/components/ml/composables/useMLState.ts
// ML 训练与分析面板状态管理

import { reactive, ref } from 'vue'
import type { GateRules } from '../featureLab/types'

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
  /**
   * 结构化告警码。Python 侧对"会改变结论"的软失败会带 code，
   * 前端据此做差异化提示（而不是把所有警告都当成同一句话）。
   * 例：no_dev_window —— 没有 DEV 选择段，指标口径退化为整段历史（乐观）。
   */
  code?: string
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
  node_label?: string        // 训练来源的 XGBoostNode label（bind 时作为 production 文件名后缀）
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

// ── 特征门禁（/v0/ml action=screen）──────────────────────────────

/** DEV 内某一折的 IC 统计 */
export interface ScreenFoldStats {
  fold_ic: (number | null)[]
  fold_t: (number | null)[]
  sign_consistency: number
  fold_ic_std: number | null
  n_folds: number
  gap: number
  effective_gap: number
  gap_raised: boolean
  /** 每折的起止日期（左闭右开） */
  fold_dates: (string[] | null)[] | null
}

/** top-k 超额 */
export interface ScreenTopk {
  k: number
  excess: number
  excess_t: number
  ls: number
  ls_t: number
  hit: number
  days: number
}

/** 单个特征的门禁结论 */
export interface ScreenFeature {
  sign: 1 | -1
  coverage: number
  q_value: number | null
  selection: {
    selected: boolean
    tier: 'A' | 'B' | 'C'
    reasons: string[]
    gate_k: number
    gate_topk: ScreenTopk
  }
  dev: {
    days: number
    coverage: number
    ic_mean: number
    ic_std: number
    hit_rate: number
    pooled_ic: number
    nw_t: number
    p_value: number
    icir: number
    ts_ic_mean?: number
    ts_ic_std?: number
    folds?: ScreenFoldStats
    topk?: Record<string, ScreenTopk>
    yearly?: Record<string, number>
  }
  test: {
    days: number
    coverage: number
    ic_mean: number
    nw_t: number
    icir: number
    hit_rate: number
    pooled_ic: number
    topk?: Record<string, ScreenTopk>
  } | null
  incremental: {
    incr_ic: number
    incr_t: number
    incr_ratio: number
    incr_days: number
    n_regressors: number
  }
  /** IC 衰减：horizon -> {ic, t, days, coverage} */
  decay: Record<string, { ic: number; t: number; days: number; coverage: number }>
  lag: {
    ic?: Record<string, number>
    autocorr1?: number
    lag_retention?: number
    verdict?: string
  } | null
}

/** screen_result 返回的完整报告 */
export interface ScreenReport {
  meta: {
    generated_at?: string
    entry?: string
    primary_horizon?: number
    horizons?: number[]
    dev_end?: string
    dev_split_end?: string
    dev_days?: number
    test_start?: string
    test_days?: number
    purge?: number
    n_folds?: number
    fold_gap?: number
    effective_gap?: number
    min_names?: number
    nw_lag?: number
    gate_topk?: number
    rules?: Record<string, number>
    m_tests?: number
    bonferroni_t?: number
    has_ohlcv?: boolean
  }
  features: Record<string, ScreenFeature>
}

/** screen 任务终态（SSE result 事件） */
export interface ScreenTaskResult {
  run_id: string
  out_dir: string
  files: { json?: string; csv?: string; md?: string }
  summary: {
    n_features: number
    n_tier_a: number
    n_tier_b: number
    m_tests: number
    bonferroni_t: number
    dev_days: number
    test_days: number
    primary_horizon: number
    lag_sensitivity: boolean
    plots: boolean
  }
  meta: Record<string, any>
}

export interface ShapResult {
  model_id: number
  features: string[]
  shap: number[][]
  base_value: number[]
  n_samples: number
  dates: string[]
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

export interface FeatureSeries {
  dates: string[]
  data: Record<string, (number | null)[]>  // null = NaN/Inf
}

export interface FeatureReport {
  total_rows: number
  n_features: number
  date_start: string
  date_end: string
  csv_path: string
  features: FeatureStat[]
  series: FeatureSeries
}

export type AnomalyType = 'nan_run' | 'jump_to_zero' | 'stale_run'

export interface Anomaly {
  type: AnomalyType
  symbol: string
  feature: string
  start_date: string
  end_date: string
  length: number
  severity: 'high' | 'mid' | 'low'
  detail: string
}

export const LABEL_TYPES = [
  { label: '自适应分类（涨/平/跌）', value: 'classification' },
  { label: '回归（收益率）', value: 'regression' },
]

export const LABEL_SHAPES = [
  { label: '多标签矩阵 (N×K)', value: 'matrix', desc: '每个标的独立标签（基于各自价格计算涨/跌/平）' },
  { label: '单标签向量 (N×1)', value: 'vector', desc: '所有标的共享一个标签（基于 labelSource 计算）' },
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

  // 特征门禁（/v0/ml action=screen）
  const screenState = reactive<{
    task: ScreenTaskResult | null   // 任务终态（summary / meta / run_id）
    report: ScreenReport | null      // 逐特征明细（screen_result）
    loading: boolean
    steps: TrainStep[]
    logs: TrainLog[]
    error: string
  }>({
    task: null,
    report: null,
    loading: false,
    steps: [],
    logs: [],
    error: '',
  })

  // 门禁规则与开关。
  // labelPeriod / volK / nFolds / foldGap / sigmaMode 仍放在 config 里与标签、训练共用；
  // 这里只承载门禁独有的阈值，供门禁表单与运行记录快照共用。
  const screenConfig = reactive<GateRules>({
    minIc: 0.02,
    minT: 2.5,
    minSignFolds: 4,
    fdrQ: 0.10,
    minCoverage: 0.5,
    gateTopk: 5,
    minNames: 15,
    trialCount: 0,
    noTest: false,
    noIncremental: false,
    noLagSensitivity: false,
  })

  // 训练配置
  const config = reactive({
    labelSource: '',         // 标签来源变量（如 "sh.600000.close"）
    labelPeriod: 5,          // 未来周期
    labelType: 'classification',
    labelShape: 'matrix',    // 'vector' = 单标签(N×1), 'matrix' = 多标签矩阵(N×K, per-symbol)
    volK: 0.5,               // 自适应阈值系数：threshold = volK × σ × √N
    sigmaMode: 'expanding',  // 标签阈值波动率口径：expanding=逐行因果(无泄露) / full=全序列(旧行为)
    objective: 'multi:softprob',
    numClass: 3,
    // 特征门禁 / 交叉验证共用参数（特征分析面板设定，训练面板只读）
    nFolds: 5,               // 门禁的 DEV 内折数（2..50）
    foldGap: 0,              // 折间间隔 bar 数；生效值 = max(foldGap, labelPeriod)
    selectedFeatures: [] as string[],   // 门禁通过、准备用于训练的特征名
    valRatio: 0.15,
    testRatio: 0.15,
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

  // 参数优化状态
  interface OptimizeTrial {
    number: number
    value: number | null
    best: number | null
    params: Record<string, number>
    duration_ms: number
    status: 'ok' | 'failed'
    error?: string
    summary?: Record<string, number>
  }

  interface OptimizeResult {
    best_params: Record<string, number>
    best_value: number
    best_trial_number: number
    best_model: string | null
    n_trials: number
    n_completed: number
    trials: OptimizeTrial[]
    importance: { name: string; importance: number }[]
    optimization_duration_ms: number
    metric: string
    output_dir: string
  }

  const optimizeResult = ref<OptimizeResult | null>(null)
  const optimizeRunning = ref(false)
  const optimizeTrials = ref<OptimizeTrial[]>([])
  const optimizeProgress = ref('')
  /** 优化过程中的告警。优化面板常驻展示，不依赖训练日志面板是否可见。 */
  const optimizeLogs = ref<TrainLog[]>([])

  const DEFAULT_PARAM_DOMAINS: Record<string, { min: number; max: number; step?: number; log?: boolean; enabled: boolean }> = {
    learning_rate:    { min: 0.005, max: 0.3,  log: true,     enabled: true },
    max_depth:        { min: 3,     max: 10,                  enabled: true },
    n_estimators:     { min: 50,    max: 500,  step: 50,      enabled: true },
    subsample:        { min: 0.5,   max: 1.0,                 enabled: true },
    colsample_bytree: { min: 0.5,   max: 1.0,                 enabled: true },
    gamma:            { min: 0,     max: 5,                   enabled: true },
    min_child_weight: { min: 1,     max: 20,                  enabled: true },
    reg_alpha:        { min: 0,     max: 10,                  enabled: true },
    reg_lambda:       { min: 0,     max: 10,                  enabled: true },
  }
  const paramDomains = reactive(JSON.parse(JSON.stringify(DEFAULT_PARAM_DOMAINS)))
  const optimizeMetric = ref('sharpe')
  const nTrials = ref(50)

  function resetOptimize() {
    optimizeResult.value = null
    optimizeRunning.value = false
    optimizeTrials.value = []
    optimizeProgress.value = ''
  }

  function applyBestParams() {
    if (!optimizeResult.value) return
    const bp = optimizeResult.value.best_params
    if (bp.learning_rate != null) config.learningRate = bp.learning_rate
    if (bp.max_depth != null) config.maxDepth = bp.max_depth
    if (bp.n_estimators != null) config.nEstimators = bp.n_estimators
    if (bp.subsample != null) config.subsample = bp.subsample
    if (bp.colsample_bytree != null) config.colsampleBytree = bp.colsample_bytree
    if (bp.gamma != null) config.gamma = bp.gamma
    if (bp.min_child_weight != null) config.minChildWeight = bp.min_child_weight
  }

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
    screenState.task = null
    screenState.report = null
    screenState.loading = false
    screenState.steps = []
    screenState.logs = []
    screenState.error = ''
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
    resetOptimize()
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
    screenState,
    screenConfig,
    config,
    QUICK_RANGES,
    setQuickRange,
    reset,
    isClassification,
    syncObjective,
    optimizeResult,
    optimizeRunning,
    optimizeTrials,
    optimizeProgress,
    paramDomains,
    optimizeMetric,
    nTrials,
    DEFAULT_PARAM_DOMAINS,
    resetOptimize,
    applyBestParams,
  }
}
