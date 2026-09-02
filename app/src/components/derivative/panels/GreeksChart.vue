<template>
  <div class="greeks-chart">
    <div class="toolbar">
      <span class="hint" v-if="!result">请先计算定价</span>
      <div v-else class="greek-selector">
        <button v-for="g in greekNames" :key="g"
          :class="['greek-btn', { active: selectedGreek === g }]"
          @click="selectedGreek = g">
          {{ g }}
        </button>
      </div>
    </div>
    <div ref="chartRef" class="chart-area"></div>
  </div>
</template>

<script setup lang="ts">
import { ref, watch, onMounted, onUnmounted } from 'vue'
import * as echarts from 'echarts'
import { priceOption, type PricingResult } from '../composables/useOptionPricing'

const props = defineProps<{
  result: PricingResult | null
  params: { spot: number; strike: number; volatility: number; risk_free_rate: number; expiry: string; method: string }
}>()

const greekNames = ['delta', 'gamma', 'theta', 'vega', 'rho']
const selectedGreek = ref('delta')
const chartRef = ref<HTMLElement>()
let chart: echarts.ECharts | null = null

async function render() {
  if (!chart || !props.result) return

  // 生成 Greeks vs spot 曲线
  const spotRange = generateSpotRange(props.params.spot, 0.3, 50)
  const callGreeks: number[] = []
  const putGreeks: number[] = []

  for (const s of spotRange) {
    try {
      const callRes = await priceOption({
        method: 'black_scholes', spot: s,
        strike: props.params.strike,
        volatility: props.params.volatility,
        risk_free_rate: props.params.risk_free_rate,
        is_call: true,
        expiry: props.params.expiry || undefined,
      })
      callGreeks.push((callRes.greeks as any)[selectedGreek.value])

      const putRes = await priceOption({
        method: 'black_scholes', spot: s,
        strike: props.params.strike,
        volatility: props.params.volatility,
        risk_free_rate: props.params.risk_free_rate,
        is_call: false,
        expiry: props.params.expiry || undefined,
      })
      putGreeks.push((putRes.greeks as any)[selectedGreek.value])
    } catch {
      callGreeks.push(0)
      putGreeks.push(0)
    }
  }

  chart.setOption({
    backgroundColor: 'transparent',
    tooltip: {
      trigger: 'axis',
      backgroundColor: 'rgba(26, 34, 54, 0.95)',
      borderColor: 'rgba(74, 85, 104, 0.3)',
      textStyle: { color: '#e0e0e0', fontSize: 12 },
    },
    legend: {
      top: 8,
      textStyle: { color: '#8899bb', fontSize: 11 },
    },
    grid: { left: 60, right: 30, top: 50, bottom: 40 },
    xAxis: {
      type: 'category',
      data: spotRange.map(s => s.toFixed(2)),
      name: '标的价格',
      nameTextStyle: { color: '#8899bb' },
      axisLabel: { color: '#8899bb', fontSize: 10 },
      axisLine: { lineStyle: { color: 'rgba(74, 85, 104, 0.3)' } },
    },
    yAxis: {
      type: 'value',
      name: selectedGreek.value.toUpperCase(),
      nameTextStyle: { color: '#8899bb' },
      axisLabel: { color: '#8899bb' },
      splitLine: { lineStyle: { color: 'rgba(74, 85, 104, 0.1)' } },
    },
    series: [
      {
        name: 'Call',
        type: 'line', data: callGreeks,
        lineStyle: { width: 2 }, itemStyle: { color: '#66bb6a' }, symbol: 'none',
      },
      {
        name: 'Put',
        type: 'line', data: putGreeks,
        lineStyle: { width: 2 }, itemStyle: { color: '#ef5350' }, symbol: 'none',
      },
    ],
  }, true)
}

function generateSpotRange(center: number, pct: number, n: number): number[] {
  const lo = center * (1 - pct)
  const hi = center * (1 + pct)
  const step = (hi - lo) / (n - 1)
  return Array.from({ length: n }, (_, i) => lo + i * step)
}

function handleResize() { chart?.resize() }

onMounted(() => {
  if (chartRef.value) chart = echarts.init(chartRef.value)
  window.addEventListener('resize', handleResize)
})

onUnmounted(() => {
  window.removeEventListener('resize', handleResize)
  chart?.dispose()
})

watch(() => [props.result, selectedGreek.value], render)
</script>

<style scoped>
.greeks-chart { height: 100%; display: flex; flex-direction: column; }
.toolbar { display: flex; align-items: center; padding: 8px 0; gap: 8px; flex-shrink: 0; }
.hint { color: #8899bb; font-size: 13px; }
.greek-selector { display: flex; gap: 4px; }
.greek-btn {
  padding: 4px 12px; background: rgba(26, 34, 54, 0.6);
  border: 1px solid rgba(74, 85, 104, 0.3); border-radius: 4px;
  color: #8899bb; font-size: 12px; cursor: pointer; text-transform: capitalize;
}
.greek-btn:hover { border-color: rgba(41, 98, 255, 0.4); color: #e0e0e0; }
.greek-btn.active { background: rgba(41, 98, 255, 0.2); border-color: #2962ff; color: #2962ff; }
.chart-area { flex: 1; min-height: 400px; }
</style>
