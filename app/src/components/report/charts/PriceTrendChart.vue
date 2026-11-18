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
import { buildPriceTrendOption, type PriceTrendPoint } from '@/lib/charts/options/priceTrend'

interface Props {
  prices: Record<string, [string, number][]>
  /** 完整标的列表（用于下拉框，不依赖已加载的价格数据） */
  symbols?: string[]
  buySignals: any[]
  sellSignals: any[]
  rawBuySignals?: any[]   // [symbol, timestamp, quantity, price][]
  rawSellSignals?: any[]  // [symbol, timestamp, quantity, price][]
  selectedSymbol?: string
}

interface Emits {
  (e: 'symbolChange', symbol: string): void
}

const props = withDefaults(defineProps<Props>(), {
  symbols: () => [],
})
const emit = defineEmits<Emits>()

// 下拉框选项：优先使用传入的完整标的列表，回退到已加载价格的 keys
const priceSymbols = computed(() =>
  props.symbols.length > 0 ? props.symbols : Object.keys(props.prices)
)

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
  const chartData = props.prices[localSymbol.value]
  if (chartData && chartData.length > 0) {
    updateChart(getPriceOption(chartData, props.sellSignals, props.buySignals, props.rawSellSignals, props.rawBuySignals, localSymbol.value), true)
  }
}

/**
 * 组装价格趋势图 option —— 调共享 builder（Agent 的 price_trend 视图同款）。
 * 这里只负责把本组件的信号数据翻译成 builder 的输入契约。
 */
function getPriceOption(chartData: any[], sellSignals: any[], buySignals: any[], rawSell?: any[], rawBuy?: any[], targetSymbol?: string) {
  // 构建日期到索引的映射
  const dateIndexMap = new Map<string, number>()
  chartData.forEach((item: any, index: number) => {
    dateIndexMap.set(item[0], index)
  })

  // 按标的过滤原始信号，转成 { index, price } 点位
  function filterSignalsBySymbol(rawSigs: any[], sym: string): PriceTrendPoint[] {
    const points: PriceTrendPoint[] = []
    const filtered = rawSigs.filter(s => s[0] === sym)
    filtered.forEach(signal => {
      const ts = signal[1] // timestamp in seconds
      const date = new Date(ts * 1000)
      const Y = date.getFullYear()
      const M = (date.getMonth() + 1) < 10 ? '0' + (date.getMonth() + 1) : '' + (date.getMonth() + 1)
      const D = '' + date.getDate()
      const dateStr = `${Y}-${M}-${D}`
      const idx = dateIndexMap.get(dateStr)
      if (idx !== undefined) points.push({ index: idx, price: chartData[idx][1] })
    })
    return points
  }

  // 旧版 formatted signals（[date, price][]）转点位
  function mapFormattedSignals(signals: any[]): PriceTrendPoint[] {
    const points: PriceTrendPoint[] = []
    signals.forEach((signal: any[]) => {
      const idx = dateIndexMap.get(signal[0])
      if (idx !== undefined) points.push({ index: idx, price: chartData[idx][1] })
    })
    return points
  }

  // 优先使用 raw signals 按标的过滤，回退到旧的 formatted signals
  const useRaw = (rawBuy?.length ?? 0) > 0 || (rawSell?.length ?? 0) > 0
  const buyPoints = useRaw
    ? (targetSymbol ? filterSignalsBySymbol(rawBuy || [], targetSymbol) : [])
    : mapFormattedSignals(buySignals)
  const sellPoints = useRaw
    ? (targetSymbol ? filterSignalsBySymbol(rawSell || [], targetSymbol) : [])
    : mapFormattedSignals(sellSignals)

  return buildPriceTrendOption({
    categories: chartData.map((item: any) => item[0]),
    prices: chartData.map((item: any) => item[1]),
    buyPoints,
    sellPoints,
  })
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

// 当前标的的价格数据异步加载完成后，刷新图表
watch(
  () => props.prices[localSymbol.value],
  (chartData) => {
    if (chartData && chartData.length > 0) {
      updateChart(getPriceOption(chartData, props.sellSignals, props.buySignals, props.rawSellSignals, props.rawBuySignals, localSymbol.value), true)
    }
  }
)

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
  background: rgba(0, 0, 0, 0.3);
  border: 1px solid var(--border, rgba(74, 158, 255, 0.3));
  border-radius: 6px;
  color: var(--text, #e2e8f0);
  padding: 5px 10px;
  font-size: 13px;
  outline: none;
  cursor: pointer;
  transition: all 0.2s ease;
  min-width: 100px;
  appearance: none;
  -webkit-appearance: none;
  background-image: url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' width='12' height='12' viewBox='0 0 12 12'%3E%3Cpath fill='%23a0aec0' d='M6 8L1 3h10z'/%3E%3C/svg%3E");
  background-repeat: no-repeat;
  background-position: right 8px center;
  padding-right: 28px;
}

.chart-controls select:hover:not(:focus) {
  border-color: rgba(41, 98, 255, 0.5);
}

.chart-controls select:focus {
  outline: none;
  border-color: var(--primary, #2962ff);
  box-shadow: 0 0 0 2px rgba(41, 98, 255, 0.2);
  background: rgba(0, 0, 0, 0.4);
}

.chart-controls select option {
  background: var(--panel-bg, #1a2236);
  color: var(--text, #e2e8f0);
  padding: 8px;
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
