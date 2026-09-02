<template>
  <div class="strategy-builder">
    <div class="toolbar">
      <select v-model="preset" @change="loadPreset">
        <option value="">自定义组合</option>
        <option value="bull_call">牛市看涨价差</option>
        <option value="bear_put">熊市看跌价差</option>
        <option value="straddle">跨式</option>
        <option value="strangle">宽跨式</option>
        <option value="butterfly">蝶式</option>
        <option value="iron_condor">铁鹰</option>
      </select>
      <button class="btn-add" @click="addLeg">+ 添加腿</button>
      <button class="btn-calc" @click="calculate">计算组合</button>
    </div>

    <!-- 腿列表 -->
    <div class="legs-list">
      <div v-for="(leg, i) in legs" :key="i" class="leg-row">
        <select v-model="leg.direction">
          <option value="long">买入</option>
          <option value="short">卖出</option>
        </select>
        <select v-model="leg.type">
          <option value="call">Call</option>
          <option value="put">Put</option>
        </select>
        <input type="number" v-model.number="leg.strike" placeholder="K" step="0.01" class="input-sm" />
        <input type="number" v-model.number="leg.quantity" placeholder="数量" min="1" class="input-sm" />
        <button class="btn-remove" @click="removeLeg(i)">×</button>
      </div>
    </div>

    <!-- 组合结果 -->
    <div v-if="netGreeks" class="result-section">
      <div class="net-greeks">
        <span class="label">组合 Greeks:</span>
        <span v-for="(v, k) in netGreeks" :key="k" class="greek-chip">
          {{ k }}={{ (v as number).toFixed(4) }}
        </span>
      </div>
      <div class="net-cost">
        净权利金: <strong>{{ netCost.toFixed(4) }}</strong>
      </div>
    </div>

    <div ref="chartRef" class="chart-area"></div>
  </div>
</template>

<script setup lang="ts">
import { ref, onMounted, onUnmounted } from 'vue'
import * as echarts from 'echarts'
import { priceOption, type PricingResult } from '../composables/useOptionPricing'

const props = defineProps<{ spot: number; riskFreeRate: number }>()

interface Leg {
  direction: 'long' | 'short'
  type: 'call' | 'put'
  strike: number
  quantity: number
}

const preset = ref('')
const legs = ref<Leg[]>([
  { direction: 'long', type: 'call', strike: props.spot, quantity: 1 },
])
const netGreeks = ref<Record<string, number> | null>(null)
const netCost = ref(0)
const chartRef = ref<HTMLElement>()
let chart: echarts.ECharts | null = null

function addLeg() {
  legs.value.push({ direction: 'long', type: 'call', strike: props.spot, quantity: 1 })
}

function removeLeg(i: number) {
  legs.value.splice(i, 1)
}

function loadPreset() {
  const s = props.spot
  const presets: Record<string, Leg[]> = {
    bull_call: [
      { direction: 'long', type: 'call', strike: s, quantity: 1 },
      { direction: 'short', type: 'call', strike: s * 1.05, quantity: 1 },
    ],
    bear_put: [
      { direction: 'long', type: 'put', strike: s, quantity: 1 },
      { direction: 'short', type: 'put', strike: s * 0.95, quantity: 1 },
    ],
    straddle: [
      { direction: 'long', type: 'call', strike: s, quantity: 1 },
      { direction: 'long', type: 'put', strike: s, quantity: 1 },
    ],
    strangle: [
      { direction: 'long', type: 'call', strike: s * 1.05, quantity: 1 },
      { direction: 'long', type: 'put', strike: s * 0.95, quantity: 1 },
    ],
    butterfly: [
      { direction: 'long', type: 'call', strike: s * 0.95, quantity: 1 },
      { direction: 'short', type: 'call', strike: s, quantity: 2 },
      { direction: 'long', type: 'call', strike: s * 1.05, quantity: 1 },
    ],
    iron_condor: [
      { direction: 'long', type: 'put', strike: s * 0.9, quantity: 1 },
      { direction: 'short', type: 'put', strike: s * 0.95, quantity: 1 },
      { direction: 'short', type: 'call', strike: s * 1.05, quantity: 1 },
      { direction: 'long', type: 'call', strike: s * 1.1, quantity: 1 },
    ],
  }
  if (preset.value && presets[preset.value]) {
    legs.value = presets[preset.value].map(l => ({ ...l }))
  }
}

async function calculate() {
  if (legs.value.length === 0) return

  const spotRange = Array.from({ length: 80 }, (_, i) => {
    const lo = props.spot * 0.7
    const hi = props.spot * 1.3
    return lo + (hi - lo) * i / 79
  })

  // 计算每条腿在每个 spot 的价格
  const legResults: Array<{ price: number; greeks: any }> = []
  const greeks: Record<string, number> = { delta: 0, gamma: 0, theta: 0, vega: 0, rho: 0 }
  let totalCost = 0

  for (const leg of legs.value) {
    try {
      const res = await priceOption({
        method: 'black_scholes',
        spot: props.spot,
        strike: leg.strike,
        volatility: 0.2,
        risk_free_rate: props.riskFreeRate,
        is_call: leg.type === 'call',
      })
      const sign = leg.direction === 'long' ? 1 : -1
      totalCost += sign * res.price * leg.quantity
      for (const k of Object.keys(greeks)) {
        greeks[k] += sign * (res.greeks as any)[k] * leg.quantity
      }
      legResults.push({ price: res.price, greeks: res.greeks })
    } catch {
      legResults.push({ price: 0, greeks: { delta: 0, gamma: 0, theta: 0, vega: 0, rho: 0 } })
    }
  }

  netGreeks.value = greeks
  netCost.value = totalCost

  // 构建组合 payoff 曲线
  const payoffNow: number[] = []
  const payoffExpiry: number[] = []

  for (const s of spotRange) {
    let nowVal = 0
    let expVal = 0
    for (let li = 0; li < legs.value.length; li++) {
      const leg = legs.value[li]
      const sign = leg.direction === 'long' ? 1 : -1
      // 到期 payoff
      const intrinsic = leg.type === 'call'
        ? Math.max(s - leg.strike, 0)
        : Math.max(leg.strike - s, 0)
      expVal += sign * intrinsic * leg.quantity
      // 当前价值: 简化用 BSM
      try {
        const res = await priceOption({
          method: 'black_scholes', spot: s, strike: leg.strike,
          volatility: 0.2, risk_free_rate: props.riskFreeRate,
          is_call: leg.type === 'call',
        })
        nowVal += sign * res.price * leg.quantity
      } catch { /* skip */ }
    }
    payoffNow.push(nowVal - totalCost)
    payoffExpiry.push(expVal - totalCost)
  }

  renderChart(spotRange, payoffNow, payoffExpiry)
}

function renderChart(spots: number[], payoffNow: number[], payoffExpiry: number[]) {
  if (!chart) return
  chart.setOption({
    backgroundColor: 'transparent',
    tooltip: {
      trigger: 'axis',
      backgroundColor: 'rgba(26, 34, 54, 0.95)',
      borderColor: 'rgba(74, 85, 104, 0.3)',
      textStyle: { color: '#e0e0e0', fontSize: 12 },
    },
    legend: { top: 8, textStyle: { color: '#8899bb', fontSize: 11 } },
    grid: { left: 60, right: 30, top: 50, bottom: 40 },
    xAxis: {
      type: 'value', name: '标的价格',
      nameTextStyle: { color: '#8899bb' },
      axisLabel: { color: '#8899bb' },
      splitLine: { lineStyle: { color: 'rgba(74, 85, 104, 0.1)' } },
    },
    yAxis: {
      type: 'value', name: '盈亏',
      nameTextStyle: { color: '#8899bb' },
      axisLabel: { color: '#8899bb' },
      splitLine: { lineStyle: { color: 'rgba(74, 85, 104, 0.1)' } },
    },
    series: [
      {
        name: '到期收益', type: 'line',
        data: spots.map((s, i) => [s, payoffExpiry[i]]),
        lineStyle: { width: 2, type: 'dashed' }, itemStyle: { color: '#8899bb' }, symbol: 'none',
      },
      {
        name: '当前理论价值', type: 'line',
        data: spots.map((s, i) => [s, payoffNow[i]]),
        lineStyle: { width: 2.5 }, itemStyle: { color: '#2962ff' }, symbol: 'none',
        areaStyle: {
          color: new echarts.graphic.LinearGradient(0, 0, 0, 1, [
            { offset: 0, color: 'rgba(41, 98, 255, 0.15)' },
            { offset: 1, color: 'rgba(41, 98, 255, 0)' }
          ])
        },
      },
      {
        name: '零线', type: 'line',
        data: spots.map(s => [s, 0]),
        lineStyle: { width: 1, type: 'dotted', color: 'rgba(255,255,255,0.2)' },
        symbol: 'none', silent: true,
      },
    ],
  }, true)
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
</script>

<style scoped>
.strategy-builder { height: 100%; display: flex; flex-direction: column; }
.toolbar { display: flex; gap: 8px; padding: 8px 0; flex-shrink: 0; align-items: center; }
.toolbar select {
  background: rgba(26, 34, 54, 0.8); border: 1px solid rgba(74, 85, 104, 0.3);
  border-radius: 4px; color: #e0e0e0; padding: 4px 8px; font-size: 12px;
}
.btn-add, .btn-calc {
  padding: 4px 12px; border: none; border-radius: 4px;
  font-size: 12px; cursor: pointer;
}
.btn-add { background: rgba(74, 85, 104, 0.3); color: #8899bb; }
.btn-add:hover { background: rgba(74, 85, 104, 0.5); }
.btn-calc { background: #2962ff; color: white; }
.btn-calc:hover { background: #1e50d9; }

.legs-list { flex-shrink: 0; margin-bottom: 8px; }
.leg-row {
  display: flex; gap: 6px; align-items: center; margin-bottom: 4px;
}
.leg-row select, .leg-row input {
  background: rgba(26, 34, 54, 0.8); border: 1px solid rgba(74, 85, 104, 0.3);
  border-radius: 4px; color: #e0e0e0; padding: 4px 6px; font-size: 12px;
}
.leg-row .input-sm { width: 80px; }
.btn-remove {
  background: rgba(239, 83, 80, 0.2); border: none; border-radius: 4px;
  color: #ef5350; cursor: pointer; padding: 4px 8px; font-size: 14px;
}

.result-section {
  flex-shrink: 0; margin-bottom: 8px; padding: 8px;
  background: rgba(26, 34, 54, 0.6); border-radius: 4px;
  border: 1px solid rgba(74, 85, 104, 0.2);
}
.net-greeks { display: flex; gap: 8px; align-items: center; flex-wrap: wrap; }
.net-greeks .label { font-size: 12px; color: #8899bb; }
.greek-chip {
  font-size: 11px; padding: 2px 6px; background: rgba(41, 98, 255, 0.1);
  border-radius: 3px; color: #8899bb; font-family: monospace;
}
.net-cost { margin-top: 4px; font-size: 12px; color: #e0e0e0; }
.net-cost strong { color: #2962ff; }

.chart-area { flex: 1; min-height: 300px; }
</style>
