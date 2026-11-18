/**
 * 价格趋势 + 买卖信号 → ECharts option
 *
 * 纯函数。report 的 PriceTrendChart 与 Agent 的 price_trend 视图共用这一份，
 * 两边画出来的图完全一致（缩放条、tooltip 样式、信号三角都是同一套）。
 *
 * 输入契约（与 report 侧的原始信号解耦）：
 *   - categories：X 轴分类标签
 *   - prices：与 categories 等长的收盘价
 *   - buyPoints / sellPoints：信号点（index 指向 categories 的下标）
 */

export interface PriceTrendPoint {
  /** 在 categories 中的下标 */
  index: number
  /** 信号处的价格 */
  price: number
}

export interface PriceTrendInput {
  categories: string[]
  prices: number[]
  buyPoints?: PriceTrendPoint[]
  sellPoints?: PriceTrendPoint[]
}

/** 把信号点摊平成与 categories 等长的数组（无信号处为 null） */
function toAligned(points: PriceTrendPoint[] | undefined, length: number): (number | null)[] {
  const out: (number | null)[] = new Array(length).fill(null)
  for (const p of points ?? []) {
    if (p.index >= 0 && p.index < length) out[p.index] = p.price
  }
  return out
}

/**
 * 构建价格趋势图 option。
 */
export function buildPriceTrendOption(input: PriceTrendInput): any {
  const { categories, prices } = input
  const mappedBuySignals = toAligned(input.buyPoints, categories.length)
  const mappedSellSignals = toAligned(input.sellPoints, categories.length)

  return {
    tooltip: {
      trigger: 'axis',
      axisPointer: {
        type: 'cross',
        label: { backgroundColor: '#6a7985' },
      },
      backgroundColor: 'rgba(26, 34, 54, 0.9)',
      borderColor: '#2a3449',
      textStyle: { color: '#e0e0e0' },
      formatter: function (params: any) {
        let result = `<div style="margin: 0 0 5px 0; font-weight: bold;">${params[0].axisValue}</div>`
        params.forEach((item: any) => {
          if (item.seriesName === '价格') {
            const price = typeof item.value === 'number' ? item.value : item.value[1]
            result += `<div>${item.marker} ${item.seriesName}: <span style="color: #2962ff; font-weight: bold;">${price.toFixed(2)}</span></div>`
          } else if (item.seriesName === '买入信号') {
            result += `<div style="color: #00c853;">${item.marker} ${item.seriesName}</div>`
          } else if (item.seriesName === '卖出信号') {
            result += `<div style="color: #ff6d00;">${item.marker} ${item.seriesName}</div>`
          }
        })
        return result
      },
    },
    legend: {
      data: ['价格', '买入信号', '卖出信号'],
      textStyle: { color: '#e0e0e0' },
      top: 'top',
      right: '10%',
    },
    grid: {
      left: '3%',
      right: '4%',
      bottom: '15%',
      containLabel: true,
    },
    dataZoom: [
      {
        type: 'inside',
        xAxisIndex: 0,
        filterMode: 'filter',
        zoomOnMouseWheel: true,
        moveOnMouseMove: true,
        moveOnMouseWheel: false,
      },
      {
        type: 'slider',
        xAxisIndex: 0,
        filterMode: 'filter',
        bottom: '3%',
        height: 20,
        borderColor: '#2a3449',
        fillerColor: 'rgba(41, 98, 255, 0.2)',
        handleStyle: { color: '#2962ff' },
        textStyle: { color: '#a0aec0' },
      },
    ],
    xAxis: {
      type: 'category',
      data: categories,
      axisLine: { lineStyle: { color: '#6E7079' } },
      axisLabel: {
        color: '#a0aec0',
        rotate: 0,
      },
      splitLine: { show: false },
    },
    yAxis: {
      type: 'value',
      scale: true,
      axisLine: { lineStyle: { color: '#6E7079' } },
      axisLabel: { color: '#a0aec0' },
      splitLine: {
        lineStyle: { color: '#2a3449', type: 'dashed' },
      },
    },
    series: [
      {
        name: '价格',
        type: 'line',
        data: prices,
        lineStyle: { width: 2 },
        itemStyle: { color: '#2962ff' },
        smooth: true,
        showSymbol: false,
        animationDuration: 2000,
        animationEasing: 'cubicOut',
      },
      {
        name: '买入信号',
        type: 'scatter',
        data: mappedBuySignals,
        symbol: 'triangle',
        symbolSize: 16,
        itemStyle: { color: '#00c853' },
        emphasis: { scale: 1.5 },
      },
      {
        name: '卖出信号',
        type: 'scatter',
        data: mappedSellSignals,
        symbol: 'triangle',
        symbolSize: 16,
        symbolRotate: 180,
        itemStyle: { color: '#ff6d00' },
        emphasis: { scale: 1.5 },
      },
    ],
  }
}
