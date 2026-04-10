// app/src/lib/statistics.ts
// 统计计算纯函数模块 - 偏度、峰度、收益率等

export interface StatisticsResult {
  /** 偏度 */
  skewness: number
  /** 峰度 */
  kurtosis: number
  /** 均值 */
  mean: number
  /** 标准差 */
  std: number
  /** 收益率序列 */
  returns: number[]
  /** 直方图数据 */
  histogram: { bins: number[]; counts: number[] }
}

/**
 * 从价格数据计算收益率序列
 * @param prices 价格数组
 * @returns 简单收益率序列
 */
export function calculateReturns(prices: number[]): number[] {
  const returns: number[] = []
  for (let i = 1; i < prices.length; i++) {
    returns.push((prices[i] - prices[i - 1]) / prices[i - 1])
  }
  return returns
}

/**
 * 计算统计量（偏度、峰度、均值、标准差）
 * @param returns 收益率序列
 * @returns 统计结果
 */
export function computeStatistics(returns: number[]): StatisticsResult {
  const n = returns.length
  if (n === 0) {
    return {
      skewness: 0,
      kurtosis: 0,
      mean: 0,
      std: 0,
      returns,
      histogram: { bins: [], counts: [] }
    }
  }

  // 计算均值
  let sum = 0
  for (let i = 0; i < n; i++) {
    sum += returns[i]
  }
  const mean = sum / n

  // 计算标准差和高阶矩
  let variance = 0
  let skewnessNum = 0
  let kurtosisNum = 0
  
  for (let i = 0; i < n; i++) {
    const diff = returns[i] - mean
    const diff2 = diff * diff
    variance += diff2
    skewnessNum += diff2 * diff
    kurtosisNum += diff2 * diff2
  }

  // 样本方差 (Bessel 校正)
  variance /= (n - 1)
  const std = Math.sqrt(variance)

  // 处理标准差为 0 的情况
  if (std === 0 || !isFinite(std)) {
    return {
      skewness: 0,
      kurtosis: 0,
      mean,
      std: 0,
      returns,
      histogram: { bins: [], counts: [] }
    }
  }

  // 偏度 (Fisher 定义)
  const skewness = (skewnessNum / n) / Math.pow(std, 3)

  // 峰度 (Pearson 定义，正态分布为 3)
  const kurtosis = (kurtosisNum / n) / (variance * variance)

  // 计算直方图数据（用于绘制实际分布）
  const histogram = computeHistogram(returns, 50)

  return {
    skewness,
    kurtosis,
    mean,
    std,
    returns,
    histogram
  }
}

/**
 * 计算直方图
 * @param data 数据数组
 * @param numBins 箱子数量
 * @returns 直方图数据
 */
function computeHistogram(
  data: number[],
  numBins: number
): { bins: number[]; counts: number[] } {
  if (data.length === 0) {
    return { bins: [], counts: [] }
  }

  let min = data[0]
  let max = data[0]
  for (let i = 1; i < data.length; i++) {
    if (data[i] < min) min = data[i]
    if (data[i] > max) max = data[i]
  }

  // 避免 min === max 导致除零
  if (min === max) {
    min -= 0.5
    max += 0.5
  }

  const binWidth = (max - min) / numBins
  const counts = new Array(numBins).fill(0)
  const bins: number[] = []

  // 计算每个箱子的中心点
  for (let i = 0; i < numBins; i++) {
    bins.push(min + binWidth * (i + 0.5))
  }

  // 统计每个数据点所属的箱子
  for (let i = 0; i < data.length; i++) {
    let binIndex = Math.floor((data[i] - min) / binWidth)
    if (binIndex >= numBins) binIndex = numBins - 1
    if (binIndex < 0) binIndex = 0
    counts[binIndex]++
  }

  // 归一化为概率密度（使得面积和为 1）
  const total = data.length * binWidth
  for (let i = 0; i < numBins; i++) {
    counts[i] /= total
  }

  return { bins, counts }
}

/**
 * 生成正态分布曲线数据
 * @param mean 均值
 * @param std 标准差
 * @param points 采样点数
 * @returns [x, y][] 数据点
 */
export function generateNormalDistribution(
  mean: number = 0,
  std: number = 1,
  points: number = 100
): number[][] {
  const data: number[][] = []
  const range = 4 * std // ±4 标准差

  for (let i = 0; i <= points; i++) {
    const x = mean - range + (2 * range * i) / points
    const y =
      (1 / (std * Math.sqrt(2 * Math.PI))) *
      Math.exp(-0.5 * Math.pow((x - mean) / std, 2))
    data.push([Number(x.toFixed(6)), Number(y.toFixed(8))])
  }

  return data
}
