// 特征配方研究台的类型定义
// 与 docs/superpowers/specs/2026-10-25-feature-recipe-lab-design.md 对应

export type RecipeNodeType = 'function' | 'formula' | 'emd'

export interface Recipe {
  id: string
  /** 节点 label，同时是 CSV 列名与公式变量名 */
  name: string
  nodeType: RecipeNodeType
  params: Record<string, any>
  /** 勾选要进入门禁的端点短名；空数组表示该节点只作为中间量 */
  selectedOutputs: string[]
}

export interface RecipeSet {
  id: string
  name: string
  universe: string[]
  frequency: string
  dateRange: [string, string]
  recipes: Recipe[]
  createdAt: string
  updatedAt: string
}

export interface OutputEndpoint {
  /** 特征短名，如 'ma5' / 'emd.nimf_0' */
  name: string
  /** 节点内的显示名，如 'nimf_0' */
  label: string
  /** 能否作为公式变量（多段名不行，见 spec §6.2） */
  formulaRef: boolean
}

/** 门禁规则与开关（labelPeriod / volK / nFolds / foldGap / sigmaMode 仍在 useMLState.config） */
export interface GateRules {
  minIc: number
  minT: number
  minSignFolds: number
  fdrQ: number
  minCoverage: number
  gateTopk: number
  minNames: number
  trialCount: number
  noTest: boolean
  noIncremental: boolean
  noLagSensitivity: boolean
}

/** 一次门禁运行的完整口径快照 */
export interface ScreenConfig extends GateRules {
  labelPeriod: number
  volK: number
  nFolds: number
  foldGap: number
  sigmaMode: string
}

export interface EndpointStats {
  /** 该端点覆盖的标的列数 */
  columns: number
  /** 各标的列 valid/total 的均值 */
  dataCoverage: number
  /** 各标的 nan_pct 的均值 */
  nanPct: number
}

export interface FeatureSnapshot {
  id: string
  createdAt: string
  recipeSetId: string | null
  recipeSetName: string
  config: {
    universe: string[]
    frequency: string
    dateRange: [string, string]
    recipes: Recipe[]
  }
  totalRows: number
  nColumns: number
  dateStart: string
  dateEnd: string
  /** 本次实际收集到的全部端点短名，门禁白名单的合法上界 */
  collectedOutputs: string[]
  features: Record<string, EndpointStats>
}

export interface RunFeatureStats {
  tier: 'A' | 'B' | 'C'
  sign: 1 | -1
  devIc: number
  nwT: number
  q: number | null
  coverage: number
  signFolds: number
  testIc: number | null
}

export interface RunSummary {
  n_features: number
  n_tier_a: number
  n_tier_b: number
  m_tests: number
  bonferroni_t: number
  dev_days: number
  test_days: number
}

export interface GateRun {
  id: string
  createdAt: string
  featureSnapshotId: string
  recipeSetId: string | null
  recipeSetName: string
  serverRunId: string
  analyzedFeatures: string[]
  gateConfig: ScreenConfig
  summary: RunSummary
  features: Record<string, RunFeatureStats>
}

export function newId(prefix: string): string {
  const rnd = (globalThis as any).crypto?.randomUUID?.()
  return rnd ? `${prefix}_${rnd}` : `${prefix}_${Date.now()}_${Math.random().toString(36).slice(2, 8)}`
}

export function todayISO(): string {
  return new Date().toISOString().slice(0, 10)
}
