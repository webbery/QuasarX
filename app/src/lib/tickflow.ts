// app/src/lib/tickflow.ts
// TickFlow API 客户端 - 基于日线数据的基准指数获取与缓存

export interface KlineData {
  time: number;        // 时间戳（秒）
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
  { code: 'SH000300', name: '沪深 300', description: 'A 股大盘蓝筹基准' },
  { code: 'SH000905', name: '中证 500', description: '中盘成长基准' },
  { code: 'SH000852', name: '中证 1000', description: '小盘基准' },
  { code: 'SH000016', name: '上证 50', description: '超大盘基准' },
  { code: 'SZ399001', name: '深证成指', description: '深证市场基准' },
  { code: 'SZ399006', name: '创业板指', description: '成长创业基准' },
  { code: 'SH000001', name: '上证指数', description: '上证全市场基准' },
];

const DB_NAME = 'quasarx-benchmark';
const DB_VERSION = 1;
const STORE_NAME = 'kline';
const CACHE_EXPIRY_MS = 6 * 60 * 60 * 1000; // 6 小时缓存

// ============ API Key 管理 ============
export function getApiKey(): string {
  return localStorage.getItem('tickflow_api_key') || '';
}

export function setApiKey(key: string): void {
  localStorage.setItem('tickflow_api_key', key);
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
  // 按天粒度缓存
  const startDay = Math.floor(start / 86400);
  const endDay = Math.floor(end / 86400);
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
  const days = (data[data.length - 1].time - data[0].time) / 86400;
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

// ============ API 调用 ============
export async function fetchBenchmark(symbol: string, start: number, end: number): Promise<BenchmarkResult> {
  const apiKey = getApiKey();
  const params = new URLSearchParams({
    symbol,
    period: '1d',  // 日线数据
    start: start.toString(),
    end: end.toString(),
  });

  const url = `https://api.tickflow.org/v1/klines?${params.toString()}`;

  const headers: Record<string, string> = { 'Accept': 'application/json' };
  if (apiKey) {
    headers['Authorization'] = `Bearer ${apiKey}`;
  }

  const res = await fetch(url, { headers });

  if (!res.ok) {
    throw new Error(`TickFlow API 错误：${res.status} ${res.statusText}`);
  }

  const result = await res.json();

  // 适配 TickFlow API 响应格式
  if (result.code !== undefined && result.code !== 0) {
    throw new Error(`TickFlow API 错误：${result.msg || '未知错误'}`);
  }

  // 支持两种响应格式：{code, data} 或 {data: [...]}
  const klineData = result.data || result;
  const data: KlineData[] = Array.isArray(klineData) ? klineData : [];

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
  const start = Math.floor(startDate.getTime() / 1000);
  const end = Math.floor(endDate.getTime() / 1000);

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

export interface CumulativeReturnPoint {
  date: string;         // YYYY-MM-DD
  timestamp: number;    // 秒
  cumulativeReturn: number;  // 累计收益（金额）
}

/**
 * 根据买卖信号和收盘价计算累计收益曲线
 * @param buySignals 买入信号 [symbol, timestamp, quantity, price][]
 * @param sellSignals 卖出信号 [symbol, timestamp, quantity, price][]
 * @param priceHistory 收盘价数据 [[date, close], ...]
 * @returns 每个时间点的累计收益
 */
export function calculateCumulativeReturns(
  buySignals: [string, number, number, number][],
  sellSignals: [string, number, number, number][],
  priceHistory: [string, number][]
): CumulativeReturnPoint[] {
  // 1. 合并买卖信号，按时间排序
  const allSignals: TradeSignal[] = [
    ...buySignals.map(s => ({ symbol: s[0], timestamp: s[1], quantity: s[2], price: s[3], side: 'buy' as const })),
    ...sellSignals.map(s => ({ symbol: s[0], timestamp: s[1], quantity: s[2], price: s[3], side: 'sell' as const }))
  ].sort((a, b) => a.timestamp - b.timestamp);

  if (allSignals.length === 0 || priceHistory.length === 0) {
    return [];
  }

  // 2. 追踪持仓：symbol -> { quantity, avgCost }
  const positions = new Map<string, { quantity: number; avgCost: number }>();

  // 3. 追踪累计盈亏
  let cumulativePnl = 0;

  // 4. 按日期计算累计收益
  const result: CumulativeReturnPoint[] = [];

  // 构建信号映射：timestamp -> 该时间点的信号列表
  const signalMap = new Map<number, TradeSignal[]>();
  for (const signal of allSignals) {
    if (!signalMap.has(signal.timestamp)) {
      signalMap.set(signal.timestamp, []);
    }
    signalMap.get(signal.timestamp)!.push(signal);
  }

  // 遍历价格历史，计算每天的累计收益
  for (const [dateStr, closePrice] of priceHistory) {
    const date = new Date(dateStr);
    const timestamp = Math.floor(date.getTime() / 1000);

    // 处理当天的交易信号
    const signals = signalMap.get(timestamp) || [];
    for (const signal of signals) {
      if (signal.side === 'buy') {
        // 买入：增加持仓
        const pos = positions.get(signal.symbol) || { quantity: 0, avgCost: 0 };
        const totalCost = pos.avgCost * pos.quantity + signal.price * signal.quantity;
        pos.quantity += signal.quantity;
        pos.avgCost = pos.quantity > 0 ? totalCost / pos.quantity : 0;
        positions.set(signal.symbol, pos);
      } else {
        // 卖出：计算盈亏，减少持仓
        const pos = positions.get(signal.symbol);
        if (pos && pos.quantity > 0) {
          const sellQty = Math.min(signal.quantity, pos.quantity);
          const pnl = (signal.price - pos.avgCost) * sellQty;
          cumulativePnl += pnl;
          pos.quantity -= sellQty;
          if (pos.quantity === 0) {
            positions.delete(signal.symbol);
          } else {
            positions.set(signal.symbol, pos);
          }
        }
      }
    }

    // 计算当天的浮动盈亏（未平仓部分）
    let unrealizedPnl = 0;
    for (const [symbol, pos] of positions.entries()) {
      if (pos.quantity > 0) {
        // 需要找到该 symbol 当天的收盘价
        const currentPrice = closePrice; // 简化：假设 priceHistory 是主要标的的价格
        unrealizedPnl += (currentPrice - pos.avgCost) * pos.quantity;
      }
    }

    result.push({
      date: dateStr,
      timestamp,
      cumulativeReturn: cumulativePnl + unrealizedPnl
    });
  }

  return result;
}
