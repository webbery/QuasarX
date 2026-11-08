// 标的代码与交易所的格式转换
//
// 本项目存在两种书写顺序，不要再在调用处各写一份：
//   前缀格式 `sh.600000` —— 策略图 / ML 链路（C++ to_symbol 的输入）
//   后缀格式 `600000.SH` —— 数据中心与行情 API（/v0/quote、/v0/stocks/history）

export type ExchangeCode = 'sh' | 'sz' | 'bj'

const EXCHANGE_RE = /^(sh|sz|bj)$/i
const CODE_RE = /^\d{6}$/

/**
 * 纯 6 位代码推断交易所。
 * 5/6 → 沪；0/1/2/3 → 深；4/8 → 北；9 → 平台合成标的（symbol_market.csv 标为 SZ）。
 * 无法判断返回 null。
 */
export function inferExchange(raw: string): ExchangeCode | null {
  const code = String(raw ?? '').trim().toLowerCase()
  if (!CODE_RE.test(code)) return null
  switch (code[0]) {
    case '5':
    case '6':
      return 'sh'
    case '0':
    case '1':
    case '2':
    case '3':
      return 'sz'
    case '4':
    case '8':
      return 'bj'
    case '9':
      return 'sz'
    default:
      return null
  }
}

// ── 前缀格式（策略图 / ML 链路）──────────────────────────────

/** `600000` → `sh.600000`；已带点的原样返回；无法判断时原样返回 */
export function toGraphSymbol(raw: string): string {
  const s = String(raw ?? '').trim().toLowerCase()
  if (!s) return ''
  if (s.includes('.')) return s
  const exchange = inferExchange(s)
  return exchange ? `${exchange}.${s}` : s
}

/** 批量转前缀格式并去重，保持首次出现顺序 */
export function toGraphSymbols(list: string[]): string[] {
  const out: string[] = []
  const seen = new Set<string>()
  for (const raw of list) {
    const symbol = toGraphSymbol(raw)
    if (!symbol || seen.has(symbol)) continue
    seen.add(symbol)
    out.push(symbol)
  }
  return out
}

// ── 后缀格式（数据中心 / 行情 API）──────────────────────────

/** `600000.SH` / `sh.600000` / `sh600000` / `600000` → `600000` */
export function symbolToId(symbol: string): string {
  const s = String(symbol ?? '').trim()
  if (!s) return ''
  const parts = s.split('.')
  if (parts.length === 2) {
    const [a, b] = parts
    if (EXCHANGE_RE.test(a) && CODE_RE.test(b)) return b
    if (CODE_RE.test(a) && EXCHANGE_RE.test(b)) return a
    return b
  }
  const m = s.match(/^(sh|sz|bj)(\d{6})$/i)
  return m ? m[2] : s
}

/** `600000.SH` / `sh.600000` / `600000` → `SH`（未知时按代码推断，兜底 SH） */
export function symbolToExchange(symbol: string): string {
  const s = String(symbol ?? '').trim()
  const parts = s.split('.')
  if (parts.length === 2) {
    const [a, b] = parts
    if (EXCHANGE_RE.test(a)) return a.toUpperCase()
    if (EXCHANGE_RE.test(b)) return b.toUpperCase()
  }
  const m = s.match(/^(sh|sz|bj)(\d{6})$/i)
  if (m) return m[1].toUpperCase()
  return (inferExchange(s) ?? 'sh').toUpperCase()
}

/** `600000.SH` / `sh.600000` / `600000` → `sh` */
export function exchangeLower(symbol: string): string {
  return symbolToExchange(symbol).toLowerCase()
}

/** 交易所前缀 → 中文简称（用于界面徽标） */
export const EXCHANGE_LABEL: Record<ExchangeCode, string> = {
  sh: '沪',
  sz: '深',
  bj: '北'
}

/**
 * 拆出交易所与纯代码：`sh.600000` / `600000.SH` / `600000` → `{ exchange: 'sh', code: '600000' }`。
 * 无法判定交易所时 exchange 为 null（调用方自行决定是否显示徽标）。
 */
export function splitSymbol(raw: string): { exchange: ExchangeCode | null; code: string } {
  const s = String(raw ?? '').trim()
  if (!s) return { exchange: null, code: '' }

  const parts = s.split('.')
  if (parts.length === 2) {
    const [a, b] = parts
    if (EXCHANGE_RE.test(a)) return { exchange: a.toLowerCase() as ExchangeCode, code: b }
    if (EXCHANGE_RE.test(b)) return { exchange: b.toLowerCase() as ExchangeCode, code: a }
    return { exchange: null, code: s }
  }

  const m = s.match(/^(sh|sz|bj)(\d{6})$/i)
  if (m) return { exchange: m[1].toLowerCase() as ExchangeCode, code: m[2] }

  if (CODE_RE.test(s)) return { exchange: inferExchange(s), code: s }
  return { exchange: null, code: s }
}

/**
 * 解析搜索框输入：`600000.SH` / `sh.600000` / `sh600000` / `600000` / `沪`。
 * 与 splitSymbol 不同：纯代码不推断交易所（`600000` 不限定为沪）。
 */
export function parseSymbolQuery(raw: string): { code: string; exchange: ExchangeCode | null } {
  const s = String(raw ?? '').trim().toLowerCase()
  if (!s) return { code: '', exchange: null }

  if (s.length === 1) {
    const hit = (Object.keys(EXCHANGE_LABEL) as ExchangeCode[]).find(k => EXCHANGE_LABEL[k] === s)
    if (hit) return { code: '', exchange: hit }
  }

  const parts = s.split('.')
  if (parts.length === 2) {
    const [a, b] = parts
    if (EXCHANGE_RE.test(a)) return { code: b, exchange: a.toLowerCase() as ExchangeCode }
    if (EXCHANGE_RE.test(b)) return { code: a, exchange: b.toLowerCase() as ExchangeCode }
    return { code: s, exchange: null }
  }

  const m = s.match(/^(sh|sz|bj)(\d{6})$/)
  if (m) return { code: m[2], exchange: m[1] as ExchangeCode }

  return { code: s, exchange: null }
}

/**
 * 搜索框匹配：代码片段命中即算匹配；查询带交易所时再限定交易所。
 * `600000` 匹配 `sz.600000`；`600000.SH` 只匹配沪市 600000；`沪` 匹配全部沪市标的。
 */
export function symbolMatches(symbol: string, query: string): boolean {
  const q = parseSymbolQuery(query)
  if (!q.code && !q.exchange) return true

  const { exchange, code } = splitSymbol(symbol)
  if (q.exchange && exchange !== q.exchange) return false
  if (!q.code) return true
  return code.toLowerCase().includes(q.code)
}

/** 转后缀格式 `600000.SH`；无法推断时用 fallback */
export function toSuffixedSymbol(raw: string, fallback: ExchangeCode = 'sh'): string {
  const s = String(raw ?? '').trim()
  if (!s) return ''
  const id = symbolToId(s)

  const parts = s.split('.')
  let exchange: ExchangeCode | null = null
  if (parts.length === 2) {
    const [a, b] = parts
    if (EXCHANGE_RE.test(a)) exchange = a.toLowerCase() as ExchangeCode
    else if (EXCHANGE_RE.test(b)) exchange = b.toLowerCase() as ExchangeCode
  }
  if (!exchange) {
    const m = s.match(/^(sh|sz|bj)(\d{6})$/i)
    if (m) exchange = m[1].toLowerCase() as ExchangeCode
  }
  if (!exchange) exchange = inferExchange(s) ?? fallback

  return `${id}.${exchange.toUpperCase()}`
}

/**
 * 是否为 A 股股票（基本面数据的有效标的）。
 *
 * BaoStock 只提供沪深 A 股的季频财报，ETF（5/1 开头）、北交所（4/8 开头）
 * 查询结果为空或直接失败，提交进去只会白跑上百次 API 调用。
 * 只接受后缀格式 `600519.SH` / `000001.SZ` / `430047.BJ`。
 */
export function isAShareStock(symbol: string): boolean {
  const m = String(symbol ?? '').trim().toUpperCase().match(/^(\d{6})\.(SH|SZ|BJ)$/)
  if (!m) return false
  const code = m[1]
  const exchange = m[2]
  if (exchange === 'SH') return code.startsWith('6')
  if (exchange === 'SZ') return /^[023]/.test(code)
  return false   // BJ：BaoStock 不认 bj. 前缀
}
