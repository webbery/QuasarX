<!-- app/src/components/report/charts/PriceTrendChart.vue -->
<!-- 价格趋势与买卖信号图 -->

<template>
  <div class="chart-card full-width">
    <div class="chart-title">
      <div class="title-icon">💹</div>
      <span>Price Trend & Trading Signals</span>
      <div class="chart-controls">
        <select v-model="localSymbol" @change="handleSymbolChange">
          <option v-for="symbol in priceSymbols" :key="symbol" :value="symbol">{{ symbol }}</option>
        </select>
      </div>
    </div>
    <div class="chart-container" ref="chartRef"></div>
  </div>
</template>

<script setup lang="ts">
import { ref, computed, watch, onMounted } from 'vue'
import * as echarts from 'echarts'
import { useECharts } from '../composables/useECharts'

interface Props {
  prices: Record<string, [string, number][]>
  buySignals: any[]
  sellSignals: any[]
  rawBuySignals?: any[]   // [symbol, timestamp, quantity, price][]
  rawSellSignals?: any[]  // [symbol, timestamp, quantity, price][]
  selectedSymbol?: string
}

interface Emits {
  (e: 'symbolChange', symbol: string): void
}

const props = defineProps<Props>()
const emit = defineEmits<Emits>()

const priceSymbols = computed(() => Object.keys(props.prices))

// Resolve localSymbol: use selectedSymbol if it exists in prices, otherwise default to first key
const localSymbol = ref('')
function resolveLocalSymbol() {
  if (!props.prices[localSymbol.value]) {
    const first = Object.keys(props.prices)[0]
    if (first) {
      localSymbol.value = first
    }
  }
}

const { chartRef, initChart, updateChart } = useECharts(true)

function handleSymbolChange() {
  emit('symbolChange', localSymbol.value)
}

function getPriceOption(chartData: any[], sellSignals: any[], buySignals: any[], rawSell?: any[], rawBuy?: any[], targetSymbol?: string) {
  // 构建日期到索引的映射
  const dateIndexMap = new Map<string, number>()
  chartData.forEach((item: any, index: number) => {
    dateIndexMap.set(item[0], index)
  })

  // 将信号数据转换为与 chartData 对齐的扁平数组
  // category 类型 x 轴按索引自动定位，只需在对应索引位置填入价格，其余为 null

  // 按标的过滤原始信号的辅助函数
  function filterSignalsBySymbol(rawSigs: any[], sym: string): number[] {
    const result = Array(chartData.length).fill(null)
    const filtered = rawSigs.filter(s => s[0] === sym)
    filtered.forEach(signal => {
      const ts = signal[1] // timestamp in seconds
      const date = new Date(ts * 1000)
      const Y = date.getFullYear()
      const M = (date.getMonth() + 1) < 10 ? '0' + (date.getMonth() + 1) : '' + (date.getMonth() + 1)
      const D = '' + date.getDate()
      const dateStr = `${Y}-${M}-${D}`
      const idx = dateIndexMap.get(dateStr)
      if (idx !== undefined) result[idx] = chartData[idx][1]
    })
    return result
  }

  // 优先使用 raw signals 按标的过滤，回退到旧的 formatted signals
  const useRaw = (rawBuy?.length ?? 0) > 0 || (rawSell?.length ?? 0) > 0
  const mappedBuySignals = useRaw
    ? (targetSymbol ? filterSignalsBySymbol(rawBuy || [], targetSymbol) : Array(chartData.length).fill(null))
    : (() => {
        const mapped = Array(chartData.length).fill(null)
        buySignals.forEach((signal: any[]) => {
          const idx = dateIndexMap.get(signal[0])
          if (idx !== undefined) mapped[idx] = chartData[idx][1]
        })
        return mapped
      })()
  const mappedSellSignals = useRaw
    ? (targetSymbol ? filterSignalsBySymbol(rawSell || [], targetSymbol) : Array(chartData.length).fill(null))
    : (() => {
        const mapped = Array(chartData.length).fill(null)
        sellSignals.forEach((signal: any[]) => {
          const idx = dateIndexMap.get(signal[0])
          if (idx !== undefined) mapped[idx] = chartData[idx][1]
        })
        return mapped
      })()

  return {
    tooltip: {
      trigger: 'axis',
      axisPointer: {
        type: 'cross',
        label: { backgroundColor: '#6a7985' }
      },
      backgroundColor: 'rgba(26, 34, 54, 0.9)',
      borderColor: '#2a3449',
      textStyle: { color: '#e0e0e0' },
      formatter: function(params: any) {
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
      }
    },
    legend: {
      data: ['价格', '买入信号', '卖出信号'],
      textStyle: { color: '#e0e0e0' },
      top: 'top',
      right: '10%'
    },
    grid: {
      left: '3%',
      right: '4%',
      bottom: '15%',
      containLabel: true
    },
    dataZoom: [
      {
        type: 'inside',
        xAxisIndex: 0,
        filterMode: 'filter',
        zoomOnMouseWheel: true,
        moveOnMouseMove: true,
        moveOnMouseWheel: false
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
        textStyle: { color: '#a0aec0' }
      }
    ],
    xAxis: {
      type: 'category',
      data: chartData.map((item: any) => item[0]),
      axisLine: { lineStyle: { color: '#6E7079' } },
      axisLabel: {
        color: '#a0aec0',
        rotate: 0
      },
      splitLine: { show: false }
    },
    yAxis: {
      type: 'value',
      scale: true,
      axisLine: { lineStyle: { color: '#6E7079' } },
      axisLabel: { color: '#a0aec0' },
      splitLine: {
        lineStyle: { color: '#2a3449', type: 'dashed' }
      }
    },
    series: [
      {
        name: '价格',
        type: 'line',
        data: chartData.map((item: any) => item[1]),
        lineStyle: { width: 2 },
        itemStyle: { color: '#2962ff' },
        smooth: true,
        showSymbol: false,
        animationDuration: 2000,
        animationEasing: 'cubicOut'
      },
      {
        name: '买入信号',
        type: 'scatter',
        data: mappedBuySignals,
        symbol: 'triangle',
        symbolSize: 16,
        itemStyle: { color: '#00c853' },
        emphasis: { scale: 1.5 }
      },
      {
        name: '卖出信号',
        type: 'scatter',
        data: mappedSellSignals,
        symbol: 'triangle',
        symbolSize: 16,
        symbolRotate: 180,
        itemStyle: { color: '#ff6d00' },
        emphasis: { scale: 1.5 }
      }
    ]
  }
}

// 监听数据变化，更新图表
watch(
  [() => props.prices, () => props.buySignals, () => props.sellSignals, () => props.rawBuySignals, () => props.rawSellSignals],
  ([prices, buys, sells]) => {
    resolveLocalSymbol()
    const chartData = prices[localSymbol.value]
    if (chartData && chartData.length > 0) {
      updateChart(getPriceOption(chartData, sells, buys, props.rawSellSignals, props.rawBuySignals, localSymbol.value), true)
    }
  },
  { deep: true }
)

// 同步 selectedSymbol 并更新图表
watch(() => props.selectedSymbol, (val) => {
  if (val) {
    localSymbol.value = val
  }
  resolveLocalSymbol()
  // selectedSymbol 变化时重新渲染图表（切换标的需要重新过滤信号）
  const prices = props.prices
  const chartData = prices[localSymbol.value]
  if (chartData && chartData.length > 0) {
    const buys = props.buySignals
    const sells = props.sellSignals
    updateChart(getPriceOption(chartData, sells, buys, props.rawSellSignals, props.rawBuySignals, localSymbol.value), true)
  }
})

onMounted(() => {
  console.info('[PriceTrendChart] 组件已挂载')
})
</script>

<style scoped>
.chart-card {
  background: var(--panel-bg, #1a2236);
  border-radius: 12px;
  padding: 20px;
  border: 1px solid var(--border, #2a3449);
  transition: all 0.3s ease;
  box-shadow: 0 4px 6px rgba(0, 0, 0, 0.1);
  display: flex;
  flex-direction: column;
  min-height: 0;
}

.chart-card:hover {
  box-shadow: 0 8px 25px rgba(0, 0, 0, 0.2);
  transform: translateY(-2px);
  border-color: #2962ff;
}

.chart-card.full-width {
  grid-column: 1 / -1;
}

.chart-title {
  font-size: 16px;
  font-weight: 600;
  margin-bottom: 16px;
  color: var(--text, #e0e0e0);
  display: flex;
  align-items: center;
  gap: 12px;
  padding-bottom: 12px;
  border-bottom: 1px solid var(--border, #2a3449);
}

.title-icon {
  font-size: 20px;
  width: 32px;
  height: 32px;
  display: flex;
  align-items: center;
  justify-content: center;
  background: rgba(41, 98, 255, 0.1);
  border-radius: 8px;
}

.chart-controls {
  margin-left: auto;
  display: flex;
  align-items: center;
  gap: 10px;
}

.chart-controls select {
  background: rgba(42, 52, 77, 0.5);
  border: 1px solid var(--border, #2a3449);
  color: var(--text, #e0e0e0);
  padding: 6px 12px;
  border-radius: 6px;
  font-size: 14px;
  cursor: pointer;
  transition: all 0.2s;
  min-width: 100px;
}

.chart-controls select:hover {
  border-color: #2962ff;
  background: rgba(41, 98, 255, 0.1);
}

.chart-controls select:focus {
  outline: none;
  border-color: #2962ff;
  box-shadow: 0 0 0 2px rgba(41, 98, 255, 0.2);
}

.chart-container {
  height: 300px;
  min-height: 300px;
  width: 100%;
  flex-shrink: 0;
}

.chart-card.full-width .chart-container {
  height: 300px;
  min-height: 300px;
}

@media (max-width: 768px) {
  .chart-card {
    padding: 16px;
  }

  .chart-container {
    height: 250px;
  }

  .chart-controls {
    flex-direction: column;
    align-items: flex-end;
    gap: 8px;
  }
}
</style>
