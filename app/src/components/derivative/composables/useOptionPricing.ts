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
  filter_stats?: FilterStats
}

export interface FilterStats {
  total_contracts: number
  filtered_count: number
  removed_by_layer: Record<string, number>
  removed_contracts: Array<{
    contract_name: string
    layer: string
    reason: string
  }>
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

// ── 单合约 meta (权利金/保证金/行权日/dte/合约单位) ──
//
// 由后端 OptionDataDB::queryByContract/queryBySymbolId 在响应顶层 meta 字段返回
// 数值字段在数据缺失时为 0 / "" — 前端按字段为 0 决定是否隐藏对应卡片
export interface OptionContractMeta {
  premium: number          // 最新结算/收盘价 = 权利金 (元/张)
  spot: number             // 标的最新收盘价 (S)
  contract_unit: number    // 合约乘数 (CFFEX IO/HO=300, MO=100; ETF=10000)
  margin: number           // 卖出开仓义务仓保证金 (元/张)
  exercise_date: string    // 行权日 "YYYY-MM-DD"
  dte: number              // 距行权日天数 (≥1)
  moneyness: string        // "ITM" / "ATM" / "OTM" / ""
  last_trade_date: string  // 数据基准日 (与 meta 对应的 trade_date)
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

// 拉取单合约 meta (复用 GET /v0/option/data?symbol_id=xxx 端点, 读顶层 meta)
// 仅拉最新一天数据 (limit=1) 减少带宽 — meta 计算在服务端基于 data 末行
export async function fetchOptionContractMeta(symbolId: string | number): Promise<OptionContractMeta | null> {
  const server = localStorage.getItem('remote')
  const token = localStorage.getItem('token')
  const url = server ? `https://${server}/v0/option/data` : `${BASE}/option/data`
  try {
    const res = await axios.get(url, {
      params: { symbol_id: symbolId, limit: 1 },
      headers: token ? { Authorization: token } : {}
    })
    if (!res.data?.meta) return null
    return res.data.meta as OptionContractMeta
  } catch (e) {
    console.error('[useOptionPricing] fetchOptionContractMeta failed:', symbolId, e)
    return null
  }
}
