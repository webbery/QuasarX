import { ref } from 'vue'
import axios from 'axios'
import { ElMessage } from 'element-plus'
import type { FundamentalData, AlignedData, FinanceRow, PriceBar } from './useFundamentalState'

export function useFundamentalData() {
  const loading = ref(false)

  async function fetchFundamental(
    symbol: string,
    startDate: string,
    endDate: string,
  ): Promise<{ data: FundamentalData; aligned: AlignedData } | null> {
    if (!symbol) {
      ElMessage.warning('请选择标的')
      return null
    }

    loading.value = true
    try {
      // 转换 symbol 格式: sh.600000 → 600000.SH (finance API 用)
      const financeCode = toFinanceCode(symbol)
      // 日期转时间戳 (stocks/history API 用)
      const startTs = Math.floor(new Date(startDate).getTime() / 1000)
      const endTs = Math.floor(new Date(endDate).getTime() / 1000)

      // 并行请求：股价 + 6 类财务数据
      const [priceResp, profitResp, growthResp, balanceResp, cashflowResp, operationResp, dupontResp] = await Promise.all([
        axios.get('/v0/stocks/history', {
          params: { id: symbol.split('.')[1], type: '1d', start: startTs, end: endTs, right: 1 },
        }),
        axios.get('/v0/finance', { params: { category: 'profit', code: financeCode } }),
        axios.get('/v0/finance', { params: { category: 'growth', code: financeCode } }),
        axios.get('/v0/finance', { params: { category: 'balance', code: financeCode } }),
        axios.get('/v0/finance', { params: { category: 'cashflow', code: financeCode } }),
        axios.get('/v0/finance', { params: { category: 'operation', code: financeCode } }),
        axios.get('/v0/finance', { params: { category: 'dupont', code: financeCode } }),
      ])

      // 解析股价数据
      const prices: PriceBar[] = parsePriceData(priceResp.data)
      if (prices.length === 0) {
        ElMessage.warning('无股价数据')
        return null
      }

      // 解析财务数据
      const data: FundamentalData = {
        prices,
        profit: parseFinanceCategory(profitResp.data, 'profit'),
        growth: parseFinanceCategory(growthResp.data, 'growth'),
        balance: parseFinanceCategory(balanceResp.data, 'balance'),
        cashflow: parseFinanceCategory(cashflowResp.data, 'cashflow'),
        operation: parseFinanceCategory(operationResp.data, 'operation'),
        dupont: parseFinanceCategory(dupontResp.data, 'dupont'),
      }

      // 对齐 + 衍生计算
      const aligned = alignAndCompute(data)

      return { data, aligned }
    } catch (err: any) {
      const msg = err.response?.data?.error || err.response?.data?.message || err.message
      ElMessage.error(`获取基本面数据失败: ${msg}`)
      console.error('[useFundamentalData]', err)
      return null
    } finally {
      loading.value = false
    }
  }

  return { loading, fetchFundamental }
}

// === 工具函数 ===

function toFinanceCode(symbol: string): string {
  // sh.600000 → 600000.SH
  const parts = symbol.split('.')
  if (parts.length === 2) {
    return `${parts[1]}.${parts[0].toUpperCase()}`
  }
  return symbol
}

function parsePriceData(raw: any): PriceBar[] {
  if (!Array.isArray(raw)) return []
  return raw.map((item: any) => ({
    datetime: typeof item.datetime === 'number'
      ? new Date(item.datetime * 1000).toISOString().slice(0, 10)
      : String(item.datetime).slice(0, 10),
    open: Number(item.open) || 0,
    close: Number(item.close) || 0,
    high: Number(item.high) || 0,
    low: Number(item.low) || 0,
    volume: Number(item.volume) || 0,
  }))
}

function parseFinanceCategory(raw: any, category: string): { category: string; name: string; count: number; data: FinanceRow[] } | null {
  if (!raw || raw.error) return null
  if (!raw.data || raw.count === 0) return null
  return {
    category,
    name: raw.name || category,
    count: raw.count || 0,
    data: raw.data || [],
  }
}

// === 核心：时间对齐 + 衍生计算 ===

function alignAndCompute(data: FundamentalData): AlignedData {
  const { prices, profit } = data
  const dates = prices.map(p => p.datetime)
  const close = prices.map(p => p.close)

  // 1. 前向填充财务数据到日线
  const profitByDate = ffillToDaily(dates, profit?.data || [])

  // 2. 计算 PE（需要 EPS_TTM）
  const pe = computePE(close, profitByDate)

  // 3. 计算 PB（暂用 ROE 近似，无 BPS 数据时返回 null）
  const pb = computePB(close, profitByDate)

  // 4. PE 分位数
  const peValues = pe.filter((v): v is number => v !== null && v > 0)
  const pePercentiles = calcPercentiles(peValues)

  // 5. 财报发布日列表
  const earningsDates = extractEarningsDates(profit?.data || [])

  return {
    dates,
    close,
    pe,
    pb,
    profitByDate,
    profitQuarters: profit?.data || [],
    growthQuarters: data.growth?.data || [],
    balanceQuarters: data.balance?.data || [],
    cashflowQuarters: data.cashflow?.data || [],
    operationQuarters: data.operation?.data || [],
    dupontQuarters: data.dupont?.data || [],
    pePercentiles,
    earningsDates,
  }
}

/**
 * 前向填充：将季度财务数据对齐到日线
 * 使用 stat_date（报告期）做对齐，避免 look-ahead bias
 */
function ffillToDaily(dailyDates: string[], financialData: FinanceRow[]): Map<string, FinanceRow> {
  const result = new Map<string, FinanceRow>()
  if (financialData.length === 0) return result

  // 按 stat_date 排序
  const sorted = [...financialData].sort((a, b) => a.stat_date.localeCompare(b.stat_date))

  let lastRow: FinanceRow | null = null
  let fi = 0

  for (const date of dailyDates) {
    // 找到所有 stat_date <= 当前日期的财务数据
    while (fi < sorted.length && sorted[fi].stat_date <= date) {
      lastRow = sorted[fi]
      fi++
    }
    if (lastRow) {
      result.set(date, lastRow)
    }
  }
  return result
}

/**
 * 计算 PE = close / EPS_TTM
 */
function computePE(close: number[], profitByDate: Map<string, FinanceRow>): (number | null)[] {
  return close.map((price, i) => {
    const date = Array.from(profitByDate.keys())[i]
    if (!date) return null
    const row = profitByDate.get(date)
    if (!row || !row.eps_ttm || row.eps_ttm <= 0) return null
    return price / row.eps_ttm
  })
}

/**
 * 计算 PB（暂返回 null，无 BPS 字段）
 */
function computePB(_close: number[], _profitByDate: Map<string, FinanceRow>): (number | null)[] {
  // TODO: 当 FinanceDB 有 BPS 字段时实现
  return []
}

/**
 * 计算分位数
 */
function calcPercentiles(values: number[]): { p25: number; p50: number; p75: number } {
  if (values.length === 0) return { p25: 0, p50: 0, p75: 0 }
  const sorted = [...values].sort((a, b) => a - b)
  return {
    p25: sorted[Math.floor(sorted.length * 0.25)],
    p50: sorted[Math.floor(sorted.length * 0.50)],
    p75: sorted[Math.floor(sorted.length * 0.75)],
  }
}

/**
 * 提取财报发布日列表
 */
function extractEarningsDates(profitData: FinanceRow[]): { date: string; type: string; label: string }[] {
  return profitData
    .filter(r => r.pub_date)
    .map(r => {
      const quarter = getQuarter(r.stat_date)
      return {
        date: r.pub_date!,
        type: 'report',
        label: `${r.stat_date.slice(0, 4)}${quarter}`,
      }
    })
}

function getQuarter(statDate: string): string {
  const month = parseInt(statDate.slice(5, 7))
  if (month <= 3) return 'Q1'
  if (month <= 6) return 'Q2'
  if (month <= 9) return 'Q3'
  return 'Q4'
}
