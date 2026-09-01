import { ref, computed, watch } from 'vue'
import axios from 'axios'
import { ElMessage } from 'element-plus'
import { getGlobalStorage } from '@/ts/globalStorage'

export type DetailTab = 'quote' | 'hfq' | 'dividend' | 'finance'

export interface HistoryBar {
  datetime: string   // YYYY-MM-DD 或 YYYY-MM-DD HH:mm:ss
  open: number
  close: number
  high: number
  low: number
  volume: number
}

export interface DividendRow {
  ex_dividend_date: string
  cash_per_10: number
  bonus_per_10: number
  transfer_per_10: number
  announce_date?: string
  report_year?: string
}

export interface FinanceRow {
  stat_date: string
  pub_date?: string
  [k: string]: any
}

const FREQ_LABELS: Record<string, string> = {
  '1d': '日频', '5m': '5分钟', '15m': '15分钟',
  '30m': '30分钟', '60m': '60分钟', '1h': '1小时',
  'daily': '日频',
}

export function getFreqLabel(freq: string): string {
  return FREQ_LABELS[freq] || freq
}

export function isDailyFreq(freq: string): boolean {
  return freq === '1d' || freq === 'daily'
}

/** '600000.SH' → '600000' */
export function symbolToId(symbol: string): string {
  const parts = symbol.split('.')
  return parts.length === 2 ? parts[1] : symbol
}

/** '600000.SH' → 'SH' */
export function symbolToExchange(symbol: string): string {
  const parts = symbol.split('.')
  return parts.length === 2 ? parts[0].toUpperCase() : 'SH'
}

/** 'SH' → 'sh' */
export function exchangeLower(symbol: string): string {
  return symbolToExchange(symbol).toLowerCase()
}

/** 从 globalStorage 缓存的 stocks Map 拿中文名 */
export function getSymbolName(symbol: string): string | null {
  const globalStorage = getGlobalStorage()
  const securities: any = globalStorage.getItem('securities')
  const stocks = securities?.stocks
  if (!stocks) return null
  return stocks.get(symbolToId(symbol)) || null
}

export function useSymbolDetailData() {
  const loading = ref(false)
  const error = ref('')
  const activeTab = ref<DetailTab>('quote')

  // 数据状态：key = `${symbol}|${tab}|${freq || ''}`
  const historyRaw = ref<HistoryBar[]>([])   // 未复权
  const historyHfq = ref<HistoryBar[]>([])   // 后复权
  const dividends = ref<DividendRow[]>([])
  const finance = ref<Record<string, FinanceRow[]>>({})  // category → rows

  // 频率（DataCenter 选中行带过来的，detail 内部展示用）
  const freq = ref<string>('1d')

  // 缓存命中：symbol 切走再切回来时直接展示已拉到的数据
  const cache: {
    historyRaw: Map<string, HistoryBar[]>
    historyHfq: Map<string, HistoryBar[]>
    dividends: Map<string, DividendRow[]>
    finance: Map<string, Record<string, FinanceRow[]>>
  } = {
    historyRaw: new Map(),
    historyHfq: new Map(),
    dividends: new Map(),
    finance: new Map(),
  }

  let currentSymbol = ''

  function historyKey(symbol: string, f: string, right: 0 | 1) {
    return `${symbol}|${f}|r${right}`
  }
  function divKey(symbol: string) {
    return symbol
  }
  function finKey(symbol: string) {
    return symbol
  }

  function resetForSymbol(symbol: string, f: string) {
    currentSymbol = symbol
    freq.value = f
    error.value = ''
    historyRaw.value = cache.historyRaw.get(historyKey(symbol, f, 0)) || []
    historyHfq.value = cache.historyHfq.get(historyKey(symbol, f, 1)) || []
    dividends.value = cache.dividends.get(divKey(symbol)) || []
    finance.value = cache.finance.get(finKey(symbol)) || {}
  }

  async function loadHistory(symbol: string, f: string, right: 0 | 1) {
    const key = historyKey(symbol, f, right)
    if (cache.historyRaw.has(key) || cache.historyHfq.has(key)) return

    const id = symbolToId(symbol)
    const ex = exchangeLower(symbol)
    const days = isDailyFreq(f) ? 365 * 2 : 30  // 日频拉 2 年，分钟级别拉 30 天
    const end = Math.floor(Date.now() / 1000)
    const start = end - days * 86400

    const resp = await axios.get('/v0/stocks/history', {
      params: { id, exchange: ex, type: f, start, end, right },
    })
    const raw = Array.isArray(resp.data) ? resp.data : (resp.data?.data || [])
    const bars: HistoryBar[] = raw.map((b: any) => ({
      datetime: typeof b.datetime === 'number'
        ? new Date(b.datetime * 1000).toISOString().slice(0, 16).replace('T', ' ')
        : String(b.datetime),
      open: Number(b.open) || 0,
      close: Number(b.close) || 0,
      high: Number(b.high) || 0,
      low: Number(b.low) || 0,
      volume: Number(b.volume) || 0,
    }))
    if (right === 0) cache.historyRaw.set(key, bars)
    else cache.historyHfq.set(key, bars)

    if (currentSymbol === symbol && freq.value === f) {
      if (right === 0) historyRaw.value = bars
      else historyHfq.value = bars
    }
  }

  async function loadDividend(symbol: string) {
    if (cache.dividends.has(divKey(symbol))) return
    const resp = await axios.get('/v0/dividend', {
      params: { code: symbol },
    })
    const rows: DividendRow[] = (resp.data?.data || []).map((r: any) => ({
      ex_dividend_date: r.ex_dividend_date,
      cash_per_10: Number(r.cash_per_10) || 0,
      bonus_per_10: Number(r.bonus_per_10) || 0,
      transfer_per_10: Number(r.transfer_per_10) || 0,
      announce_date: r.announce_date,
      report_year: r.report_year,
    }))
    cache.dividends.set(divKey(symbol), rows)
    if (currentSymbol === symbol) dividends.value = rows
  }

  async function loadFinance(symbol: string) {
    if (cache.finance.has(finKey(symbol))) return
    const categories = ['profit', 'growth', 'balance', 'cashflow', 'operation', 'dupont']
    const results: Record<string, FinanceRow[]> = {}
    await Promise.all(categories.map(async (cat) => {
      try {
        const resp = await axios.get('/v0/finance', { params: { category: cat, code: symbol } })
        results[cat] = (resp.data?.data || []) as FinanceRow[]
      } catch {
        results[cat] = []
      }
    }))
    cache.finance.set(finKey(symbol), results)
    if (currentSymbol === symbol) finance.value = results
  }

  /**
   * 主调用入口。symbol: '600000.SH'，freq: '1d' / '5m' 等。
   * 不传 tab 时只加载当前激活 tab 的数据（懒加载）。
   */
  async function setSymbol(symbol: string, f: string, tab?: DetailTab) {
    if (!symbol) return
    loading.value = true
    error.value = ''
    try {
      resetForSymbol(symbol, f)
      const target = tab || activeTab.value
      switch (target) {
        case 'quote':
          await loadHistory(symbol, f, 0)
          break
        case 'hfq':
          await loadHistory(symbol, f, 1)
          break
        case 'dividend':
          await loadDividend(symbol)
          break
        case 'finance':
          await loadFinance(symbol)
          break
      }
    } catch (e: any) {
      const msg = e.response?.data?.error || e.response?.data?.message || e.message
      error.value = msg || '加载失败'
      ElMessage.warning(`[${symbol}] ${error.value}`)
    } finally {
      loading.value = false
    }
  }

  async function switchTab(tab: DetailTab) {
    activeTab.value = tab
    if (!currentSymbol) return
    // 已缓存过则跳过；未缓存则拉取
    switch (tab) {
      case 'quote':
        if (!cache.historyRaw.has(historyKey(currentSymbol, freq.value, 0))) {
          await setSymbol(currentSymbol, freq.value, tab)
        }
        break
      case 'hfq':
        if (!cache.historyHfq.has(historyKey(currentSymbol, freq.value, 1))) {
          await setSymbol(currentSymbol, freq.value, tab)
        }
        break
      case 'dividend':
        if (!cache.dividends.has(divKey(currentSymbol))) {
          await setSymbol(currentSymbol, freq.value, tab)
        }
        break
      case 'finance':
        if (!cache.finance.has(finKey(currentSymbol))) {
          await setSymbol(currentSymbol, freq.value, tab)
        }
        break
    }
  }

  /** 后复权 vs 未复权 派生复权因子序列（按时间对齐） */
  const adjFactorSeries = computed<{ datetime: string; factor: number }[]>(() => {
    if (!historyRaw.value.length || !historyHfq.value.length) return []
    const map = new Map(historyRaw.value.map(b => [b.datetime, b.close]))
    const out: { datetime: string; factor: number }[] = []
    for (const hfq of historyHfq.value) {
      const qfq = map.get(hfq.datetime)
      if (qfq && qfq > 0) {
        out.push({ datetime: hfq.datetime, factor: hfq.close / qfq })
      }
    }
    return out
  })

  /** 最新一日复权因子（最右点） */
  const latestAdjFactor = computed(() => {
    const s = adjFactorSeries.value
    return s.length ? s[s.length - 1].factor : null
  })

  /** 分红汇总：累计派息/送股 */
  const dividendSummary = computed(() => {
    let cash = 0, bonus = 0, transfer = 0
    for (const r of dividends.value) {
      cash += r.cash_per_10
      bonus += r.bonus_per_10
      transfer += r.transfer_per_10
    }
    return { cash, bonus, transfer, count: dividends.value.length }
  })

  /** 基本面摘要：最新一个 stat_date 的关键字段 */
  const financeSummary = computed(() => {
    const out: Record<string, any> = {}
    for (const cat of Object.keys(finance.value)) {
      const rows = finance.value[cat]
      if (rows.length) out[cat] = rows[rows.length - 1]
    }
    return out
  })

  return {
    // state
    loading,
    error,
    activeTab,
    freq,
    historyRaw,
    historyHfq,
    dividends,
    finance,
    // actions
    setSymbol,
    switchTab,
    // derived
    adjFactorSeries,
    latestAdjFactor,
    dividendSummary,
    financeSummary,
    // utils
    getSymbolName,
    symbolToId,
    isDailyFreq,
  }
}