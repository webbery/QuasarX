<template>
  <div class="profit-chart">
    <div class="header-row">
      <div class="position-toggle">
        <span class="label">持仓方向</span>
        <button :class="{ active: position === 'long' }" @click="position = 'long'">买入 (Long)</button>
        <button :class="{ active: position === 'short' }" @click="position = 'short'">卖出 (Short)</button>
      </div>
      <div class="premium-info">
        <span class="label">权利金</span>
        <span class="value">{{ premiumDisplay }}</span>
        <span class="unit">元/张</span>
        <span v-if="premiumSource !== 'market'" class="source-tag">{{ premiumSourceLabel }}</span>
      </div>
    </div>
    <div v-show="!hasCurve" class="empty-hint">
      <i class="fas fa-chart-area"></i>
      请先在左侧选择合约并点击「计算定价」
    </div>
    <div v-show="hasCurve" ref="chartRef" class="chart-area"></div>
  </div>
</template>

<script setup lang="ts">
import { ref, computed, watch, onMounted, onUnmounted } from 'vue'
import * as echarts from 'echarts'
import type { PricingResult } from '../composables/useOptionPricing'

const props = defineProps<{
  result: PricingResult | null
  premium: number
  spot: number
  strike: number
  isCall: boolean
}>()

const position = ref<'long' | 'short'>('long')

const effectivePremium = computed(() => {
  if (props.premium > 0) return props.premium
  return props.result?.price ?? 0
})

const premiumSource = computed<'market' | 'theoretical' | 'none'>(() => {
  if (props.premium > 0) return 'market'
  if (props.result && props.result.price > 0) return 'theoretical'
  return 'none'
})

const premiumSourceLabel = computed(() => {
  return premiumSource.value === 'theoretical' ? '理论价兜底' : ''
})

const premiumDisplay = computed(() => {
  const v = effectivePremium.value
  return v > 0 ? v.toFixed(4) : '—'
})

const hasCurve = computed(() => {
  return !!(props.result?.payoff_curve?.length && effectivePremium.value > 0)
})

const breakEven = computed<number | null>(() => {
  const p = effectivePremium.value
  if (!p || !props.strike) return null
  return props.isCall ? props.strike + p : props.strike - p
})

const chartRef = ref<HTMLElement>()
let chart: echarts.ECharts | null = null

function render() {
  if (!hasCurve.value) return
  // 确保 chart 已初始化 (延迟 init，避免 0×0 容器)
  if (!ensureChart()) return
  // 容器尺寸为 0 时跳过渲染 (v-show=false 时 display:none)
  // 避免 echarts-gl 全局副作用导致 "Dom has no width or height" 错误
  if (!chartRef.value || chartRef.value.offsetWidth === 0 || chartRef.value.offsetHeight === 0) return

  chart.resize()

  const series: any[] = []
  const prem = effectivePremium.value
  const sign = position.value === 'long' ? 1 : -1
  const spots = props.result!.payoff_curve.map(p => p.spot)

  // 零线（盈亏分界）
  series.push({
    name: '盈亏平衡线',
    type: 'line',
    data: spots.map(s => [s, 0]),
    lineStyle: { width: 1, type: 'dotted', color: 'rgba(255,255,255,0.25)' },
    symbol: 'none',
    silent: true,
  })

  // 到期盈亏
  series.push({
    name: '到期盈亏',
    type: 'line',
    data: props.result!.payoff_curve.map(p => [p.spot, sign * (p.payoff_at_expiry - prem)]),
    lineStyle: { width: 2, type: 'dashed' },
    itemStyle: { color: '#8899bb' },
    symbol: 'none',
  })

  // 当前理论盈亏（实线 + 渐变填充）
  const fillColor = position.value === 'long'
    ? new echarts.graphic.LinearGradient(0, 0, 0, 1, [
        { offset: 0, color: 'rgba(41, 98, 255, 0.18)' },
        { offset: 1, color: 'rgba(41, 98, 255, 0)' },
      ])
    : new echarts.graphic.LinearGradient(0, 1, 0, 0, [
        { offset: 0, color: 'rgba(41, 98, 255, 0.18)' },
        { offset: 1, color: 'rgba(41, 98, 255, 0)' },
      ])

  series.push({
    name: '当前理论盈亏',
    type: 'line',
    data: props.result!.payoff_curve.map(p => [p.spot, sign * (p.payoff_now - prem)]),
    lineStyle: { width: 2.5 },
    itemStyle: { color: '#2962ff' },
    symbol: 'none',
    areaStyle: { color: fillColor },
    markLine: {
      silent: true,
      symbol: 'none',
      data: buildMarkLines(spots),
    },
    markPoint: {
      symbol: 'circle',
      symbolSize: 8,
      itemStyle: { color: '#ffc107', borderColor: '#1a2236', borderWidth: 2 },
      label: { color: '#ffc107', fontSize: 10, formatter: '当前' },
      data: props.spot ? [{ coord: [props.spot, sign * (props.result!.price - prem)] }] : [],
    },
  })

  chart.setOption({
    backgroundColor: 'transparent',
    tooltip: {
      trigger: 'axis',
      backgroundColor: 'rgba(26, 34, 54, 0.95)',
      borderColor: 'rgba(74, 85, 104, 0.3)',
      textStyle: { color: '#e0e0e0', fontSize: 12 },
      formatter: (params: any[]) => {
        const x = params[0]?.axisValue
        let s = `<div style="margin-bottom:4px;color:#8899bb;">当前价格 S = ${Number(x).toFixed(4)}</div>`
        params
          .filter(p => p.seriesName !== '盈亏平衡线')
          .forEach(p => {
            const v = (p.value as number[])[1]
            const color = v >= 0 ? '#66bb6a' : '#ef5350'
            const signStr = v >= 0 ? '+' : ''
            s += `<div>${p.marker} ${p.seriesName}: <span style="color:${color};font-weight:600;">${signStr}${v.toFixed(4)}</span></div>`
          })
        return s
      },
    },
    legend: {
      top: 8,
      textStyle: { color: '#8899bb', fontSize: 11 },
    },
    grid: { left: 70, right: 30, top: 50, bottom: 50 },
    xAxis: {
      type: 'value',
      name: '当前价格',
      nameTextStyle: { color: '#8899bb', padding: [10, 0, 0, 0] },
      axisLine: { lineStyle: { color: 'rgba(74, 85, 104, 0.3)' } },
      axisLabel: { color: '#8899bb' },
      splitLine: { lineStyle: { color: 'rgba(74, 85, 104, 0.1)' } },
    },
    yAxis: {
      type: 'value',
      name: '利润',
      nameTextStyle: { color: '#8899bb' },
      axisLine: { lineStyle: { color: 'rgba(74, 85, 104, 0.3)' } },
      axisLabel: {
        color: '#8899bb',
        formatter: (v: number) => v.toFixed(2),
      },
      splitLine: { lineStyle: { color: 'rgba(74, 85, 104, 0.1)' } },
    },
    dataZoom: [
      { type: 'inside', xAxisIndex: 0 },
      { type: 'inside', yAxisIndex: 0 },
    ],
    series,
  }, true)
}

function buildMarkLines(spots: number[]) {
  const lines: any[] = []
  const minS = Math.min(...spots)
  const maxS = Math.max(...spots)

  if (props.strike && props.strike >= minS && props.strike <= maxS) {
    lines.push({
      xAxis: props.strike,
      lineStyle: { color: 'rgba(255, 193, 7, 0.7)', type: 'dashed', width: 1.5 },
      label: {
        formatter: `行权价 K = ${props.strike}`,
        color: '#ffc107',
        fontSize: 11,
        fontWeight: 600,
        position: 'insideEndTop',
        backgroundColor: 'rgba(26, 34, 54, 0.75)',
        padding: [2, 4],
        borderRadius: 3,
      },
    })
  }
  if (props.spot && props.spot >= minS && props.spot <= maxS) {
    lines.push({
      xAxis: props.spot,
      lineStyle: { color: 'rgba(102, 187, 106, 0.5)', type: 'dashed', width: 1 },
      label: {
        formatter: `当前 S = ${props.spot}`,
        color: '#66bb6a',
        fontSize: 10,
        position: 'insideEndBottom',
        backgroundColor: 'rgba(26, 34, 54, 0.75)',
        padding: [1, 3],
      },
    })
  }
  const be = breakEven.value
  if (be !== null && be >= minS && be <= maxS) {
    lines.push({
      xAxis: be,
      lineStyle: { color: 'rgba(239, 83, 80, 0.55)', type: 'dotted', width: 1 },
      label: {
        formatter: `盈亏平衡 ${be.toFixed(4)}`,
        color: '#ef5350',
        fontSize: 10,
        position: 'insideStartTop',
        backgroundColor: 'rgba(26, 34, 54, 0.75)',
        padding: [1, 3],
      },
    })
  }
  return lines
}

function handleResize() { chart?.resize() }

let resizeObserver: ResizeObserver | null = null

// 延迟初始化 echarts，只在容器有尺寸时才 init
function ensureChart() {
  if (!chart && chartRef.value && chartRef.value.offsetWidth > 0 && chartRef.value.offsetHeight > 0) {
    chart = echarts.init(chartRef.value)
  }
  return chart
}

onMounted(() => {
  // 设置 ResizeObserver，当容器从隐藏变为可见时触发初始化
  if (chartRef.value) {
    resizeObserver = new ResizeObserver(() => {
      if (chartRef.value && chartRef.value.offsetWidth > 0) {
        ensureChart()  // 首次激活时初始化
        if (chart) {
          chart.resize()
          if (hasCurve.value) render()
        }
      }
    })
    resizeObserver.observe(chartRef.value)
  }
  window.addEventListener('resize', handleResize)
})

onUnmounted(() => {
  resizeObserver?.disconnect()
  window.removeEventListener('resize', handleResize)
  chart?.dispose()
})

watch(
  () => [
    props.result,
    props.premium,
    props.spot,
    props.strike,
    props.isCall,
    position.value,
  ],
  () => render(),
  { deep: true },
)
</script>

<style scoped>
.profit-chart {
  height: 100%;
  display: flex;
  flex-direction: column;
  gap: 10px;
}

.header-row {
  display: flex;
  align-items: center;
  justify-content: space-between;
  flex-wrap: wrap;
  gap: 12px;
  padding: 4px 0 8px;
  border-bottom: 1px solid rgba(74, 85, 104, 0.2);
}

.position-toggle {
  display: flex;
  align-items: center;
  gap: 6px;
}

.position-toggle .label,
.premium-info .label {
  font-size: 12px;
  color: #8899bb;
  margin-right: 4px;
}

.position-toggle button {
  padding: 4px 10px;
  background: rgba(26, 34, 54, 0.6);
  border: 1px solid rgba(74, 85, 104, 0.3);
  border-radius: 4px;
  color: #8899bb;
  font-size: 12px;
  cursor: pointer;
  transition: all 0.15s;
}

.position-toggle button:hover {
  color: #e0e0e0;
  border-color: rgba(41, 98, 255, 0.4);
}

.position-toggle button.active {
  background: rgba(41, 98, 255, 0.2);
  color: #2962ff;
  border-color: rgba(41, 98, 255, 0.5);
}

.premium-info {
  display: flex;
  align-items: baseline;
  gap: 6px;
  font-size: 12px;
}

.premium-info .value {
  font-size: 14px;
  font-weight: 600;
  color: #ffc107;
  font-variant-numeric: tabular-nums;
}

.premium-info .unit {
  font-size: 11px;
  color: #6b7a99;
}

.source-tag {
  font-size: 10px;
  color: #8899bb;
  background: rgba(255, 193, 7, 0.12);
  border: 1px solid rgba(255, 193, 7, 0.3);
  padding: 1px 5px;
  border-radius: 3px;
  margin-left: 4px;
}

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

.chart-area {
  flex: 1;
  width: 100%;
  min-height: 400px;
}
</style>
