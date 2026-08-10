import { reactive, ref } from 'vue'

export interface MMARData {
  q_values: number[]
  hq: number[]
  tau_q: number[]
  multifractal_spectrum: {
    alpha: number[]
    f_alpha: number[]
  }
  hurst: number
  width: number
}

export interface PhaseSpaceData {
  embed_dim: number
  time_delay: number
  delay_method: string
  correlation_dimension: number
  max_lyapunov: number
  is_deterministic: boolean
  diagnosis: string
  trajectory: number[][]
  trajectory_time: number[]
  corr_r_values: number[]
  corr_c_values: number[]
  lyap_divergence: number[]
}

export interface NonlinearResult {
  symbol: string
  dates: string[]
  returns: number[]
  data_points: number
  mmar: MMARData
  phase_space: PhaseSpaceData
}

export interface NonlinearState {
  field: string
  dateRange: [string, string] | null
  quickRange: string
  // MMAR params
  qMin: number
  qMax: number
  qStep: number
  minWindow: number
  // Phase space params
  embedDim: number
  timeDelay: number
  lyapunovHorizon: number
  loading: boolean
}

function formatDate(d: Date): string {
  return d.toISOString().slice(0, 10)
}

export const QUICK_RANGES: [string, () => [string, string]][] = [
  ['近1年', () => {
    const end = new Date(); const start = new Date()
    start.setFullYear(start.getFullYear() - 1)
    return [formatDate(start), formatDate(end)]
  }],
  ['近3年', () => {
    const end = new Date(); const start = new Date()
    start.setFullYear(start.getFullYear() - 3)
    return [formatDate(start), formatDate(end)]
  }],
  ['近5年', () => {
    const end = new Date(); const start = new Date()
    start.setFullYear(start.getFullYear() - 5)
    return [formatDate(start), formatDate(end)]
  }],
  ['近10年', () => {
    const end = new Date(); const start = new Date()
    start.setFullYear(start.getFullYear() - 10)
    return [formatDate(start), formatDate(end)]
  }],
  ['全部', () => ['', '']]
]

export function useNonlinearState() {
  const state = reactive<NonlinearState>({
    field: 'close',
    dateRange: null,
    quickRange: '近5年',
    qMin: -5,
    qMax: 5,
    qStep: 0.5,
    minWindow: 10,
    embedDim: 3,
    timeDelay: 0,
    lyapunovHorizon: 50,
    loading: false,
  })

  const result = ref<NonlinearResult | null>(null)

  function setQuickRange(label: string) {
    const entry = QUICK_RANGES.find(([l]) => l === label)
    if (entry) {
      state.quickRange = label
      state.dateRange = entry[1]()
    }
  }

  // 初始化默认日期
  setQuickRange('近5年')

  return { state, result, setQuickRange }
}
