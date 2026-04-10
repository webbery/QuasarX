// app/src/workers/statistics.worker.ts
// Web Worker - 后台计算统计量

import { calculateReturns, computeStatistics } from '@/lib/statistics'

// Worker 全局类型
interface WorkerMessage {
  prices: Array<[string, number]> // [date, close][]
}

self.onmessage = function (e: MessageEvent<WorkerMessage>) {
  const startTime = performance.now()
  const { prices } = e.data

  try {
    // 1. 提取价格数据
    const priceArray: number[] = new Array(prices.length)
    for (let i = 0; i < prices.length; i++) {
      priceArray[i] = prices[i][1]
    }

    // 2. 计算收益率序列
    const returns = calculateReturns(priceArray)

    // 3. 计算统计量
    const stats = computeStatistics(returns)

    const endTime = performance.now()
    const duration = endTime - startTime

    // 4. 返回结果
    self.postMessage({
      success: true,
      skewness: stats.skewness,
      kurtosis: stats.kurtosis,
      mean: stats.mean,
      std: stats.std,
      histogram: stats.histogram,
      dataPoints: prices.length,
      duration: Number(duration.toFixed(2))
    })
  } catch (error) {
    self.postMessage({
      success: false,
      error: error instanceof Error ? error.message : 'Unknown error'
    })
  }
}

export {}
