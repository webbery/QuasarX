import { reactive } from 'vue'

// === 类型定义 ===

export interface PriceBar {
  datetime: string  // YYYY-MM-DD
  open: number
  close: number
  high: number
  low: number
  volume: number
}

export interface FinanceRow {
  code: string
  stat_date: string
  pub_date: string | null
  [key: string]: any
}

export interface FinanceCategory {
  category: string
  name: string
  count: number
  data: FinanceRow[]
}

export interface FundamentalData {
  prices: PriceBar[]
  profit: FinanceCategory | null
  growth: FinanceCategory | null
  balance: FinanceCategory | null
  cashflow: FinanceCategory | null
  operation: FinanceCategory | null
  dupont: FinanceCategory | null
}

export interface AlignedData {
  dates: string[]
  close: number[]
  pe: (number | null)[]
  pb: (number | null)[]
  // 按日期前向填充的财务数据
  profitByDate: Map<string, FinanceRow>
  // 季度数据（原始）
  profitQuarters: FinanceRow[]
  growthQuarters: FinanceRow[]
  balanceQuarters: FinanceRow[]
  cashflowQuarters: FinanceRow[]
  operationQuarters: FinanceRow[]
  dupontQuarters: FinanceRow[]
  // PE 分位
  pePercentiles: { p25: number; p50: number; p75: number }
  // 财报发布日列表
  earningsDates: { date: string; type: string; label: string }[]
}

export interface FundamentalState {
  symbol: string
  dateRange: [string, string]
  quickRange: string
  loading: boolean
  data: FundamentalData | null
  aligned: AlignedData | null
  // 可配置项
  heatmapMetrics: string[]
  scatterYDays: number
  valuationBands: [number, number, number]  // [p25, p50, p75]
}

// === 指标定义 ===

export const PROFIT_METRICS = [
  { key: 'roe_avg', label: 'ROE' },
  { key: 'np_margin', label: '净利率' },
  { key: 'gp_margin', label: '毛利率' },
  { key: 'eps_ttm', label: 'EPS(TTM)' },
] as const

export const GROWTH_METRICS = [
  { key: 'yoy_equity', label: '净资产增速' },
  { key: 'yoy_asset', label: '总资产增速' },
  { key: 'yoy_ni', label: '净利润增速' },
  { key: 'yoy_eps_basic', label: 'EPS增速' },
] as const

export const HEATMAP_DEFAULT_METRICS = [
  'roe_avg', 'np_margin', 'yoy_ni',
]

export const ALL_HEATMAP_METRICS = [
  { key: 'roe_avg', label: 'ROE', category: 'profit' },
  { key: 'np_margin', label: '净利率', category: 'profit' },
  { key: 'gp_margin', label: '毛利率', category: 'profit' },
  { key: 'eps_ttm', label: 'EPS(TTM)', category: 'profit' },
  { key: 'yoy_ni', label: '净利润增速', category: 'growth' },
  { key: 'yoy_eps_basic', label: 'EPS增速', category: 'growth' },
  { key: 'yoy_equity', label: '净资产增速', category: 'growth' },
  { key: 'debt_to_asset', label: '资产负债率', category: 'balance' },
  { key: 'current_ratio', label: '流动比率', category: 'balance' },
  { key: 'net_cf_act', label: '经营现金流', category: 'cashflow' },
] as const

export const QUICK_RANGES: [string, () => [string, string]][] = [
  ['近1年', () => {
    const end = new Date()
    const start = new Date()
    start.setFullYear(start.getFullYear() - 1)
    return [fmt(start), fmt(end)]
  }],
  ['近3年', () => {
    const end = new Date()
    const start = new Date()
    start.setFullYear(start.getFullYear() - 3)
    return [fmt(start), fmt(end)]
  }],
  ['近5年', () => {
    const end = new Date()
    const start = new Date()
    start.setFullYear(start.getFullYear() - 5)
    return [fmt(start), fmt(end)]
  }],
  ['近10年', () => {
    const end = new Date()
    const start = new Date()
    start.setFullYear(start.getFullYear() - 10)
    return [fmt(start), fmt(end)]
  }],
]

function fmt(d: Date): string {
  return d.toISOString().slice(0, 10)
}

// === Composable ===

export function useFundamentalState() {
  const defaultRange = QUICK_RANGES[2][1]()  // 近5年

  const state = reactive<FundamentalState>({
    symbol: '',
    dateRange: defaultRange,
    quickRange: '近5年',
    loading: false,
    data: null,
    aligned: null,
    heatmapMetrics: [...HEATMAP_DEFAULT_METRICS],
    scatterYDays: 30,
    valuationBands: [25, 50, 75],
  })

  function setQuickRange(label: string) {
    const entry = QUICK_RANGES.find(([l]) => l === label)
    if (entry) {
      state.dateRange = entry[1]()
      state.quickRange = label
    }
  }

  return { state, QUICK_RANGES, setQuickRange }
}
