// app/src/components/xgboost/composables/useXGBoostState.ts
// XGBoost 训练与分析面板状态管理

import { reactive, ref } from 'vue'

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

export const XGBOOST_OBJECTIVES = [
  { label: '二分类 (binary:logistic)', value: 'binary:logistic' },
  { label: '多分类 (multi:softprob)', value: 'multi:softprob' },
  { label: '回归 (reg:squarederror)', value: 'reg:squarederror' },
]

export const LABEL_TYPES = [
  { label: '分类（涨/跌）', value: 'classification' },
  { label: '回归（收益率）', value: 'regression' },
]

export function useXGBoostState() {
  const selectedStrategyId = ref<string>('')
  const trainResult = reactive<{
    data: TrainResult | null
    shap: ShapResult | null
    loading: boolean
    progressMsg: string
  }>({
    data: null,
    shap: null,
    loading: false,
    progressMsg: '',
  })

  // 训练配置
  const config = reactive({
    labelSource: '',         // 标签来源变量（如 "sh.600000.close"）
    labelPeriod: 5,          // 未来周期
    labelType: 'classification',
    threshold: 0.0,
    objective: 'binary:logistic',
    numClass: 2,
    testRatio: 0.2,
    learningRate: 0.1,
    maxDepth: 6,
    nEstimators: 200,
    earlyStoppingRounds: 20,
  })

  function reset() {
    trainResult.data = null
    trainResult.shap = null
    trainResult.progressMsg = ''
    trainResult.loading = false
  }

  function isClassification() {
    return config.objective === 'binary:logistic' || config.objective === 'multi:softprob'
  }

  return {
    selectedStrategyId,
    trainResult,
    config,
    reset,
    isClassification,
  }
}
