// app/src/lib/tickflow.ts
// TickFlow API 客户端 - 基于日线数据的基准指数获取与缓存

export interface KlineData {
  time: number;        // 时间戳（毫秒）
  open: number;
  high: number;
  low: number;
  close: number;
  volume: number;
}

export interface BenchmarkMetrics {
  annual_return: number;      // 年化收益率
  max_drawdown: number;       // 最大回撤
  sharp: number;              // 夏普比率
  volatility: number;         // 年化波动率
  total_return: number;       // 总收益率
  calmar_ratio?: number;      // 卡玛比率
  win_rate?: number;          // 胜率
}

export interface BenchmarkResult {
  symbol: string;
  name: string;
  data: KlineData[];
  metrics: BenchmarkMetrics;
  cachedAt?: number;
}

export interface BenchmarkIndex {
  code: string;
  name: string;
  description?: string;
}

// 支持的基准指数列表
export const BENCHMARK_INDICES: BenchmarkIndex[] = [
  { code: 'SH000001', name: '上证指数', description: '上证全市场基准' },
  { code: 'SH000300', name: '沪深 300', description: 'A 股大盘蓝筹基准' },
  { code: 'SH000905', name: '中证 500', description: '中盘成长基准' },
  { code: 'SH000852', name: '中证 1000', description: '小盘基准' },
  { code: 'SH000016', name: '上证 50', description: '超大盘基准' },
  { code: 'SZ399001', name: '深证成指', description: '深证市场基准' },
  { code: 'SZ399006', name: '创业板指', description: '成长创业基准' },
];

const DB_NAME = 'quasarx-benchmark';
const DB_VERSION = 1;
const STORE_NAME = 'kline';
const CACHE_EXPIRY_MS = 6 * 60 * 60 * 1000; // 6 小时缓存

// ============ API Key & Source 管理 ============
export function getApiKey(): string {
  return localStorage.getItem('tickflow_api_key') || '';
}

export function setApiKey(key: string): void {
  localStorage.setItem('tickflow_api_key', key);
}

export function getApiBaseUrl(): string {
  const key = localStorage.getItem('tickflow_api_key');
  if (!key) {
    return 'https://free-api.tickflow.org';
  }
  return 'https://api.tickflow.org';
}

// ============ IndexedDB 封装 ============
function openDB(): Promise<IDBDatabase> {
  return new Promise((resolve, reject) => {
    const request = indexedDB.open(DB_NAME, DB_VERSION);

    request.onerror = () => reject(request.error);
    request.onsuccess = () => resolve(request.result);

    request.onupgradeneeded = (event) => {
      const db = (event.target as IDBOpenDBRequest).result;
      if (!db.objectStoreNames.contains(STORE_NAME)) {
        db.createObjectStore(STORE_NAME, { keyPath: 'key' });
      }
    };
  });
}

function getCacheKey(symbol: string, start: number, end: number): string {
  // 按天粒度缓存（输入为毫秒级时间戳）
  const startDay = Math.floor(start / 86400000);
  const endDay = Math.floor(end / 86400000);
  return `${symbol.toUpperCase()}:${startDay}:${endDay}`;
}

// ============ 缓存管理 ============
export async function getCached(
  symbol: string,
  start: number,
  end: number
): Promise<BenchmarkResult | null> {
  try {
    const db = await openDB();
    const tx = db.transaction(STORE_NAME, 'readonly');
    const store = tx.objectStore(STORE_NAME);
    const key = getCacheKey(symbol, start, end);

    return new Promise((resolve) => {
      const req = store.get(key);
      req.onsuccess = () => {
        const cached = req.result;
        if (cached && Date.now() - cached.cachedAt < CACHE_EXPIRY_MS) {
          console.log(`[TickFlow] 缓存命中：${symbol}`);
          resolve(cached);
        } else {
          resolve(null);
        }
      };
      req.onerror = () => resolve(null);
    });
  } catch (e) {
    console.warn('[TickFlow] 读取缓存失败:', e);
    return null;
  }
}

export async function setCached(
  result: BenchmarkResult,
  start: number,
  end: number
): Promise<void> {
  try {
    const db = await openDB();
    const tx = db.transaction(STORE_NAME, 'readwrite');
    const store = tx.objectStore(STORE_NAME);

    const cache = {
      ...result,
      key: getCacheKey(result.symbol, start, end),
      cachedAt: Date.now(),
    };

    await new Promise<void>((resolve, reject) => {
      const req = store.put(cache);
      req.onsuccess = () => resolve();
      req.onerror = () => reject(req.error);
    });

    console.log(`[TickFlow] 已缓存 ${result.symbol} 数据，${result.data.length} 条`);
  } catch (e) {
    console.warn('[TickFlow] 写入缓存失败:', e);
  }
}

export async function clearAllCache(): Promise<void> {
  try {
    const db = await openDB();
    const tx = db.transaction(STORE_NAME, 'readwrite');
    const store = tx.objectStore(STORE_NAME);
    await store.clear();
    console.log('[TickFlow] 已清除所有缓存');
  } catch (e) {
    console.warn('[TickFlow] 清除缓存失败:', e);
  }
}

export async function clearExpiredCache(): Promise<void> {
  try {
    const db = await openDB();
    const tx = db.transaction(STORE_NAME, 'readwrite');
    const store = tx.objectStore(STORE_NAME);
    const now = Date.now();

    const allCaches = await new Promise<any[]>((resolve) => {
      const req = store.getAll();
      req.onsuccess = () => resolve(req.result || []);
    });

    let cleared = 0;
    for (const cache of allCaches) {
      if (now - cache.cachedAt > CACHE_EXPIRY_MS) {
        await store.delete(cache.key);
        cleared++;
      }
    }

    if (cleared > 0) {
      console.log(`[TickFlow] 已清除 ${cleared} 条过期缓存`);
    }
  } catch (e) {
    console.warn('[TickFlow] 清理缓存失败:', e);
  }
}

// ============ 指标计算（基于日线数据） ============
export function calculateMetrics(data: KlineData[]): BenchmarkMetrics {
  if (data.length < 2) {
    return {
      annual_return: 0,
      max_drawdown: 0,
      sharp: 0,
      volatility: 0,
      total_return: 0,
    };
  }

  const closes = data.map(d => d.close);
  const dailyReturns: number[] = [];

  // 计算日收益率
  for (let i = 1; i < closes.length; i++) {
    dailyReturns.push((closes[i] - closes[i - 1]) / closes[i - 1]);
  }

  // 总收益率
  const totalReturn = (closes[closes.length - 1] - closes[0]) / closes[0];

  // 年化收益率
  const days = (data[data.length - 1].time - data[0].time) / 86400000;
  const years = days / 365.25;
  const annualReturn = years > 0 ? Math.pow(1 + totalReturn, 1 / years) - 1 : 0;

  // 最大回撤
  let peak = closes[0];
  let maxDrawdown = 0;
  for (const close of closes) {
    if (close > peak) peak = close;
    const drawdown = (close - peak) / peak;
    if (drawdown < maxDrawdown) maxDrawdown = drawdown;
  }

  // 波动率 (年化)
  const meanReturn = dailyReturns.reduce((a, b) => a + b, 0) / dailyReturns.length;
  const variance = dailyReturns.reduce((sum, r) => sum + Math.pow(r - meanReturn, 2), 0) / dailyReturns.length;
  const volatility = Math.sqrt(variance * 252);

  // 夏普比率 (无风险利率 3%)
  const rf = 0.03;
  const excessReturn = annualReturn - rf;
  const sharp = volatility > 0 ? excessReturn / volatility : 0;

  // 卡玛比率
  const calmar = Math.abs(maxDrawdown) > 0 ? annualReturn / Math.abs(maxDrawdown) : 0;

  // 胜率
  const positiveDays = dailyReturns.filter(r => r > 0).length;
  const winRate = dailyReturns.length > 0 ? positiveDays / dailyReturns.length : 0;

  return {
    annual_return: parseFloat(annualReturn.toFixed(4)),
    max_drawdown: parseFloat(maxDrawdown.toFixed(4)),
    sharp: parseFloat(sharp.toFixed(2)),
    volatility: parseFloat(volatility.toFixed(4)),
    total_return: parseFloat(totalReturn.toFixed(4)),
    calmar_ratio: parseFloat(calmar.toFixed(2)),
    win_rate: parseFloat(winRate.toFixed(4)),
  };
}

// ============ 实时行情 ============
export interface QuoteExt {
  type: string;
  amplitude: number;
  change_amount: number;     // 涨跌额
  change_pct: number;        // 涨跌幅 (%)
  name: string;
  turnover_rate: number;
}

export interface QuoteData {
  symbol: string;
  amount: number;
  high: number;
  last_price: number;
  low: number;
  open: number;
  prev_close: number;        // 前收盘价
  region: string;
  timestamp: number;
  volume: number;
  ext?: QuoteExt;
  session?: string;
}

export interface QuoteResult {
  code: number;
  msg: string;
  data: QuoteData[];
}

export async function fetchQuotes(symbols: string[]): Promise<QuoteData[]> {
  const apiKey = getApiKey();
  const baseUrl = getApiBaseUrl();

  const apiSymbols = symbols.map(s => convertSymbolToApiFormat(s));
  const params = new URLSearchParams({ symbols: apiSymbols.join(',') });

  const url = `${baseUrl}/v1/quotes?${params.toString()}`;

  const headers: Record<string, string> = { 'Accept': 'application/json' };
  if (apiKey) {
    headers['x-api-key'] = apiKey;
  }

  const res = await fetch(url, { headers });

  if (!res.ok) {
    throw new Error(`TickFlow Quotes API 错误：${res.status} ${res.statusText}`);
  }

  const result: QuoteResult = await res.json();

  if (result.code !== undefined && result.code !== 0) {
    throw new Error(`TickFlow Quotes API 错误：${result.msg || '未知错误'}`);
  }

  // 将 ext 中的字段提升到顶层以便使用
  return (result.data || []).map(q => {
    if (q.ext) {
      return { ...q, ext: q.ext };
    }
    return q;
  });
}

// ============ Symbol 格式转换 ============
export function convertSymbolToApiFormat(symbol: string): string {
  // SH000300 → 000300.SH, SZ399001 → 399001.SZ
  const match = symbol.match(/^([A-Z]+)(\d+)$/);
  if (!match) return symbol;
  const [, prefix, code] = match;
  const exchange = prefix === 'SH' ? 'SH' : 'SZ';
  return `${code}.${exchange}`;
}

// ============ API 调用 ============
export async function fetchBenchmark(symbol: string, start: number, end: number): Promise<BenchmarkResult> {
  const apiKey = getApiKey();

  // 转换 symbol 格式：SH000300 → 000300.SH, SZ399001 → 399001.SZ
  const apiSymbol = convertSymbolToApiFormat(symbol);

  const params = new URLSearchParams({
    symbol: apiSymbol,
    period: '1d',
    count: '10000', // 最大数量
    start_time: start.toString(),
    end_time: end.toString(),
    adjust: 'backward',
  });

  const url = `${getApiBaseUrl()}/v1/klines?${params.toString()}`;

  const headers: Record<string, string> = { 'Accept': 'application/json' };
  if (apiKey) {
    headers['x-api-key'] = apiKey;
  }

  const res = await fetch(url, { headers });

  if (!res.ok) {
    throw new Error(`TickFlow API 错误：${res.status} ${res.statusText}`);
  }

  const result = await res.json();

  if (result.code !== undefined && result.code !== 0) {
    throw new Error(`TickFlow API 错误：${result.msg || '未知错误'}`);
  }

  // 解析响应格式：{"data": {"close": [...], "timestamp": [...], "open": [...], "high": [...], "low": [...], "volume": [...]}}
  const dataObj = result.data || {};
  const timestamps = dataObj.timestamp || [];
  const closes = dataObj.close || [];
  const opens = dataObj.open || [];
  const highs = dataObj.high || [];
  const lows = dataObj.low || [];
  const volumes = dataObj.volume || [];

  const data: KlineData[] = timestamps.map((t: number, i: number) => ({
    time: t,
    open: opens[i] || 0,
    high: highs[i] || 0,
    low: lows[i] || 0,
    close: closes[i] || 0,
    volume: volumes[i] || 0,
  }));

  if (data.length === 0) {
    throw new Error('未获取到 K 线数据');
  }

  const metrics = calculateMetrics(data);

  const indexInfo = BENCHMARK_INDICES.find(i => i.code === symbol);

  return {
    symbol,
    name: indexInfo?.name || symbol,
    data,
    metrics,
  };
}

// ============ 统一接口（带缓存） ============
export async function getBenchmark(
  symbol: string,
  startDate: Date,
  endDate: Date
): Promise<BenchmarkResult> {
  const start = startDate.getTime();  // 毫秒级时间戳
  const end = endDate.getTime();      // 毫秒级时间戳

  // 1. 尝试读取缓存
  const cached = await getCached(symbol, start, end);
  if (cached) {
    return cached;
  }

  // 2. 从 API 获取
  const fresh = await fetchBenchmark(symbol, start, end);

  // 3. 写入缓存
  await setCached(fresh, start, end);

  return fresh;
}

// ============ 工具函数 ============
export function formatBenchmarkName(symbol: string): string {
  const index = BENCHMARK_INDICES.find(i => i.code === symbol);
  return index ? `${index.name} (${index.code})` : symbol;
}

export function getBenchmarkDescription(symbol: string): string {
  const index = BENCHMARK_INDICES.find(i => i.code === symbol);
  return index?.description || '';
}

// ============ 回测收益曲线计算 ============
export interface TradeSignal {
  symbol: string;
  timestamp: number;    // 秒
  quantity: number;
  price: number;
  side: 'buy' | 'sell';
}
