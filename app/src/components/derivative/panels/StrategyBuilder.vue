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
        <!-- 合约选择下拉 -->
        <select v-model="leg.contractId" @change="onContractSelect(i)" class="contract-select">
          <option value="">自定义...</option>
          <option v-for="c in props.contracts" :key="c.symbol_id" :value="c.symbol_id">
            {{ c.contract_name }} ({{ c.strike_price }})
          </option>
        </select>
        <select v-model="leg.type" :disabled="!!leg.contractId">
          <option value="call">Call</option>
          <option value="put">Put</option>
        </select>
        <input type="number" v-model.number="leg.strike" placeholder="K" step="0.01" class="input-sm" :disabled="!!leg.contractId" />
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
        净权利金: <strong :class="{ negative: netCost < 0 }">{{ netCost.toFixed(4) }}</strong>
      </div>
    </div>

    <div v-show="!hasChart" class="empty-hint">
      <i class="fas fa-layer-group"></i>
      配置组合腿后点击「计算组合」生成利润图
    </div>
    <div v-show="hasChart" ref="chartRef" class="chart-area"></div>
  </div>
</template>

<script setup lang="ts">
import { ref, nextTick, onMounted, onUnmounted } from 'vue'
import * as echarts from 'echarts'
import { priceMultiOption, type PricingResult } from '../composables/useOptionPricing'

const props = defineProps<{
  spot: number
  riskFreeRate: number
  contracts?: Array<{
    symbol_id: number
    contract_name: string
    call_put: string
    strike_price: number
  }>
}>()

interface Leg {
  direction: 'long' | 'short'
  type: 'call' | 'put'
  strike: number
  quantity: number
  contractId?: number  // 关联的合约 ID (可选)
  contractName?: string  // 合约名称 (显示用)
}

const preset = ref('')
const legs = ref<Leg[]>([
  { direction: 'long', type: 'call', strike: props.spot, quantity: 1 },
])
const netGreeks = ref<Record<string, number> | null>(null)
const netCost = ref(0)
const hasChart = ref(false)
const chartRef = ref<HTMLElement>()
let chart: echarts.ECharts | null = null

/* 缓存最近一次计算结果，用于 resize / watch 重绘 */
let cachedSpots: number[] = []
let cachedPayoffNow: number[] = []
let cachedPayoffExpiry: number[] = []

function addLeg() {
  legs.value.push({ direction: 'long', type: 'call', strike: props.spot, quantity: 1 })
}

function removeLeg(i: number) {
  legs.value.splice(i, 1)
}

/** 从合约下拉选择后自动填充 type + strike */
function onContractSelect(i: number) {
  const leg = legs.value[i]
  if (!leg.contractId || !props.contracts) return
  const c = props.contracts.find(x => x.symbol_id === leg.contractId)
  if (!c) return
  leg.contractName = c.contract_name
  leg.type = (c.call_put === '认购' || c.call_put === 'call' || c.call_put === 'C') ? 'call' : 'put'
  leg.strike = c.strike_price
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

/** 在 payoff_curve 上对给定 spot 做线性插值 */
function interpolateCurve(curve: Array<{ spot: number; payoff_at_expiry: number; payoff_now: number }>, s: number) {
  if (curve.length === 0) return { now: 0, expiry: 0 }
  if (s <= curve[0].spot) return { now: curve[0].payoff_now, expiry: curve[0].payoff_at_expiry }
  if (s >= curve[curve.length - 1].spot) {
    const last = curve[curve.length - 1]
    return { now: last.payoff_now, expiry: last.payoff_at_expiry }
  }
  /* 二分查找 */
  let lo = 0, hi = curve.length - 1
  while (hi - lo > 1) {
    const mid = (lo + hi) >> 1
    if (curve[mid].spot <= s) lo = mid; else hi = mid
  }
  const t = (s - curve[lo].spot) / (curve[hi].spot - curve[lo].spot)
  return {
    now: curve[lo].payoff_now + t * (curve[hi].payoff_now - curve[lo].payoff_now),
    expiry: curve[lo].payoff_at_expiry + t * (curve[hi].payoff_at_expiry - curve[lo].payoff_at_expiry),
  }
}

async function calculate() {
  if (legs.value.length === 0) return

  /* 一次请求拿到所有腿的定价 + payoff_curve (100 点/腿) */
  let results: PricingResult[]
  try {
    results = await priceMultiOption(
      props.spot,
      legs.value.map(l => ({ strike: l.strike, is_call: l.type === 'call' })),
      'black_scholes',
      0.2,
      props.riskFreeRate,
    )
  } catch {
    return
  }
  if (results.length !== legs.value.length) return

  /* 聚合 Greeks + 净权利金 */
  const greeks: Record<string, number> = { delta: 0, gamma: 0, theta: 0, vega: 0, rho: 0 }
  let totalCost = 0
  for (let i = 0; i < legs.value.length; i++) {
    const leg = legs.value[i]
    const res = results[i]
    const sign = leg.direction === 'long' ? 1 : -1
    totalCost += sign * res.price * leg.quantity
    for (const k of Object.keys(greeks)) {
      greeks[k] += sign * (res.greeks as any)[k] * leg.quantity
    }
  }
  netGreeks.value = greeks
  netCost.value = totalCost

  /* 构建公共 spot 网格：取所有腿 payoff_curve 的 spot 范围之并集 */
  const allCurveSpots = results.flatMap(r => r.payoff_curve.map(p => p.spot))
  const lo = Math.min(...allCurveSpots)
  const hi = Math.max(...allCurveSpots)
  const N = 120
  const spotRange = Array.from({ length: N }, (_, i) => lo + (hi - lo) * i / (N - 1))

  /* 对每条腿的 payoff_curve 做线性插值，按方向/数量加权求和 */
  const payoffNow: number[] = []
  const payoffExpiry: number[] = []

  for (const s of spotRange) {
    let nowVal = 0
    let expVal = 0
    for (let i = 0; i < legs.value.length; i++) {
      const leg = legs.value[i]
      const sign = leg.direction === 'long' ? 1 : -1
      const curve = results[i].payoff_curve
      /* 线性插值 */
      const pt = interpolateCurve(curve, s)
      nowVal += sign * pt.now * leg.quantity
      expVal += sign * pt.expiry * leg.quantity
    }
    payoffNow.push(nowVal - totalCost)
    payoffExpiry.push(expVal - totalCost)
  }

  cachedSpots = spotRange
  cachedPayoffNow = payoffNow
  cachedPayoffExpiry = payoffExpiry

  /* 首次渲染：确保 chart 实例存在 */
  if (!hasChart.value) {
    hasChart.value = true
    await nextTick()
    if (chartRef.value && !chart) {
      chart = echarts.init(chartRef.value)
      resizeObserver = new ResizeObserver(() => {
        if (chart && chartRef.value && chartRef.value.offsetWidth > 0) {
          chart.resize()
          renderChart()
        }
      })
      resizeObserver.observe(chartRef.value)
    }
  }
  renderChart()
}

function renderChart() {
  if (!chart || cachedSpots.length === 0) return
  chart.resize()

  const spots = cachedSpots
  const minS = spots[0]
  const maxS = spots[spots.length - 1]

  /* 构建 markLine：各行权价 + 当前 spot + 盈亏平衡 */
  const markLines: any[] = []

  /* 各行权价 (黄色虚线) */
  const uniqueStrikes = [...new Set(legs.value.map(l => l.strike))].sort((a, b) => a - b)
  for (const k of uniqueStrikes) {
    if (k >= minS && k <= maxS) {
      markLines.push({
        xAxis: k,
        lineStyle: { color: 'rgba(255, 193, 7, 0.6)', type: 'dashed', width: 1.5 },
        label: {
          formatter: `K=${k.toFixed(2)}`,
          color: '#ffc107',
          fontSize: 10,
          fontWeight: 600,
          position: 'insideEndTop',
          backgroundColor: 'rgba(26, 34, 54, 0.75)',
          padding: [2, 4],
          borderRadius: 3,
        },
      })
    }
  }

  /* 当前 spot (绿线) */
  if (props.spot >= minS && props.spot <= maxS) {
    markLines.push({
      xAxis: props.spot,
      lineStyle: { color: 'rgba(102, 187, 106, 0.5)', type: 'dashed', width: 1 },
      label: {
        formatter: `S=${props.spot.toFixed(3)}`,
        color: '#66bb6a',
        fontSize: 10,
        position: 'insideEndBottom',
        backgroundColor: 'rgba(26, 34, 54, 0.75)',
        padding: [1, 3],
      },
    })
  }

  /* 当前 spot 上的盈亏 markPoint */
  const currentIdx = spots.reduce((best, s, i) =>
    Math.abs(s - props.spot) < Math.abs(spots[best] - props.spot) ? i : best, 0)
  const currentPnL = cachedPayoffNow[currentIdx]

  const series: any[] = [
    {
      name: '到期收益',
      type: 'line',
      data: spots.map((s, i) => [s, cachedPayoffExpiry[i]]),
      lineStyle: { width: 2, type: 'dashed' },
      itemStyle: { color: '#8899bb' },
      symbol: 'none',
    },
    {
      name: '当前理论盈亏',
      type: 'line',
      data: spots.map((s, i) => [s, cachedPayoffNow[i]]),
      lineStyle: { width: 2.5 },
      itemStyle: { color: '#2962ff' },
      symbol: 'none',
      areaStyle: {
        color: new echarts.graphic.LinearGradient(0, 0, 0, 1, [
          { offset: 0, color: 'rgba(41, 98, 255, 0.18)' },
          { offset: 1, color: 'rgba(41, 98, 255, 0)' },
        ]),
      },
      markLine: { silent: true, symbol: 'none', data: markLines },
      markPoint: {
        symbol: 'circle',
        symbolSize: 8,
        itemStyle: { color: '#ffc107', borderColor: '#1a2236', borderWidth: 2 },
        label: {
          color: '#ffc107',
          fontSize: 10,
          formatter: `当前 ${currentPnL >= 0 ? '+' : ''}${currentPnL.toFixed(3)}`,
        },
        data: [{ coord: [props.spot, currentPnL] }],
      },
    },
    {
      name: '零线',
      type: 'line',
      data: spots.map(s => [s, 0]),
      lineStyle: { width: 1, type: 'dotted', color: 'rgba(255,255,255,0.2)' },
      symbol: 'none',
      silent: true,
    },
  ]

  chart.setOption({
    backgroundColor: 'transparent',
    tooltip: {
      trigger: 'axis',
      backgroundColor: 'rgba(26, 34, 54, 0.95)',
      borderColor: 'rgba(74, 85, 104, 0.3)',
      textStyle: { color: '#e0e0e0', fontSize: 12 },
      formatter: (params: any[]) => {
        const x = params[0]?.axisValue
        let s = `<div style="margin-bottom:4px;color:#8899bb;">标的价格 S = ${Number(x).toFixed(4)}</div>`
        params
          .filter(p => p.seriesName !== '零线')
          .forEach(p => {
            const v = (p.value as number[])[1]
            const color = v >= 0 ? '#66bb6a' : '#ef5350'
            const signStr = v >= 0 ? '+' : ''
            s += `<div>${p.marker} ${p.seriesName}: <span style="color:${color};font-weight:600;">${signStr}${v.toFixed(4)}</span></div>`
          })
        return s
      },
    },
    legend: { top: 8, textStyle: { color: '#8899bb', fontSize: 11 } },
    grid: { left: 70, right: 30, top: 50, bottom: 50 },
    xAxis: {
      type: 'value',
      name: '标的价格',
      nameTextStyle: { color: '#8899bb', padding: [10, 0, 0, 0] },
      axisLine: { lineStyle: { color: 'rgba(74, 85, 104, 0.3)' } },
      axisLabel: { color: '#8899bb' },
      splitLine: { lineStyle: { color: 'rgba(74, 85, 104, 0.1)' } },
    },
    yAxis: {
      type: 'value',
      name: '盈亏',
      nameTextStyle: { color: '#8899bb' },
      axisLine: { lineStyle: { color: 'rgba(74, 85, 104, 0.3)' } },
      axisLabel: { color: '#8899bb', formatter: (v: number) => v.toFixed(2) },
      splitLine: { lineStyle: { color: 'rgba(74, 85, 104, 0.1)' } },
    },
    dataZoom: [
      { type: 'inside', xAxisIndex: 0 },
      { type: 'inside', yAxisIndex: 0 },
    ],
    series,
  }, true)
}

function handleResize() { chart?.resize() }

let resizeObserver: ResizeObserver | null = null

onMounted(() => {
  window.addEventListener('resize', handleResize)
})

onUnmounted(() => {
  resizeObserver?.disconnect()
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
.leg-row .contract-select {
  flex: 1.5;
  min-width: 140px;
}
.leg-row .input-sm { width: 80px; }
.leg-row select:disabled, .leg-row input:disabled {
  opacity: 0.5;
  cursor: not-allowed;
}
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
.net-cost strong.negative { color: #ef5350; }

.empty-hint {
  flex: 1;
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  color: #556;
  font-size: 13px;
  gap: 8px;
  min-height: 360px;
}
.empty-hint i {
  font-size: 36px;
  color: #3a455f;
}

.chart-area { flex: 1; min-height: 400px; }
</style>
