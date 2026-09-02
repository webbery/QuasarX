import axios from 'axios'

const BASE = '/v0'

export interface PricingRequest {
  method: 'black_scholes' | 'monte_carlo' | 'binomial'
  spot: number
  strike: number
  expiry?: string
  T?: number
  volatility: number
  risk_free_rate?: number
  dividend_yield?: number
  is_call: boolean
  is_american?: boolean
  n_paths?: number
  n_steps?: number
}

export interface Greeks {
  delta: number
  gamma: number
  theta: number
  vega: number
  rho: number
}

export interface PayoffPoint {
  spot: number
  payoff_at_expiry: number
  payoff_now: number
}

export interface PricingResult {
  price: number
  intrinsic_value: number
  time_value: number
  moneyness: string
  greeks: Greeks
  payoff_curve: PayoffPoint[]
  mc_std_error?: number
  early_exercise_premium?: number
  // multi 模式额外字段
  strike?: number
  is_call?: boolean
}

export interface IVPoint {
  strike: number
  expiry_days: number
  iv: number
  contract_name: string
  call_put: string
}

export interface IVSurfaceResult {
  raw_points: IVPoint[]
  strikes: number[]
  expiry_days: number[]
  surface: number[][]
  count: number
}

export interface ContractInfo {
  symbol_id: number
  exchange: string
  product: string
  contract_name: string
  call_put: string
  strike_price: number
  underlying: string
  start_date: string
  end_date: string
  count: number
}

export async function priceOption(req: PricingRequest): Promise<PricingResult> {
  const res = await axios.post(`${BASE}/option/pricing`, req)
  return res.data
}

export async function priceMultiOption(
  spot: number,
  contracts: Array<{ strike: number; is_call: boolean; expiry?: string; T?: number; volatility?: number; is_american?: boolean }>,
  method: string = 'black_scholes',
  volatility?: number,
  risk_free_rate?: number
): Promise<PricingResult[]> {
  const res = await axios.post(`${BASE}/option/pricing_multi`, {
    method, spot, contracts, volatility, risk_free_rate
  })
  return res.data
}

export async function getIVSurface(exchange: string, product: string): Promise<IVSurfaceResult> {
  const res = await axios.get(`${BASE}/option/iv_surface`, { params: { exchange, product } })
  return res.data
}

export async function listOptionContracts(exchange?: string, product?: string): Promise<ContractInfo[]> {
  const params: Record<string, string> = {}
  if (exchange) params.exchange = exchange
  if (product) params.product = product
  const res = await axios.get(`${BASE}/option/data`, { params })
  return res.data.contracts || []
}
