<template>
  <div class="payoff-chart">
    <div ref="chartRef" class="chart-area"></div>
  </div>
</template>

<script setup lang="ts">
import { ref, watch, onMounted, onUnmounted } from 'vue'
import * as echarts from 'echarts'
import type { PricingResult, PayoffPoint } from '../composables/useOptionPricing'

const props = defineProps<{
  result: PricingResult | null
  multiResults: PricingResult[]
  params: { strike: number; spot: number }
}>()

const chartRef = ref<HTMLElement>()
let chart: echarts.ECharts | null = null

function render() {
  if (!chart || !chartRef.value) return

  const series: echarts.EChartsOption['series'] = []
  const colors = ['#2962ff', '#ef5350', '#66bb6a', '#ffa726', '#ab47bc', '#26c6da']

  // 单合约结果
  if (props.result?.payoff_curve?.length) {
    const curve = props.result.payoff_curve
    series.push({
      name: '到期收益',
      type: 'line',
      data: curve.map(p => [p.spot, p.payoff_at_expiry]),
      lineStyle: { width: 2, type: 'dashed' },
      itemStyle: { color: '#8899bb' },
      symbol: 'none',
    })
    series.push({
      name: '当前理论价值',
      type: 'line',
      data: curve.map(p => [p.spot, p.payoff_now]),
      lineStyle: { width: 2.5 },
      itemStyle: { color: '#2962ff' },
      symbol: 'none',
      areaStyle: {
        color: new echarts.graphic.LinearGradient(0, 0, 0, 1, [
          { offset: 0, color: 'rgba(41, 98, 255, 0.15)' },
          { offset: 1, color: 'rgba(41, 98, 255, 0)' }
        ])
      },
    })
  }

  // 多合约叠加
  props.multiResults.forEach((r, i) => {
    if (!r.payoff_curve?.length) return
    const color = colors[i % colors.length]
    series.push({
      name: `K=${r.strike} ${r.is_call ? 'C' : 'P'}`,
      type: 'line',
      data: r.payoff_curve.map(p => [p.spot, p.payoff_now]),
      lineStyle: { width: 1.5 },
      itemStyle: { color },
      symbol: 'none',
    })
  })

  // 零线
  const spots = props.result?.payoff_curve?.map(p => p.spot) || []
  if (spots.length) {
    series.push({
      name: '盈亏平衡',
      type: 'line',
      data: spots.map(s => [s, 0]),
      lineStyle: { width: 1, type: 'dotted', color: 'rgba(255,255,255,0.2)' },
      symbol: 'none',
      silent: true,
    })
  }

  // 行权价标记线
  const markLines: any[] = []
  if (props.params.strike) {
    markLines.push({
      xAxis: props.params.strike,
      lineStyle: { color: 'rgba(255, 193, 7, 0.5)', type: 'dashed', width: 1 },
      label: { formatter: `K=${props.params.strike}`, color: '#ffc107', fontSize: 10 }
    })
  }
  if (props.params.spot) {
    markLines.push({
      xAxis: props.params.spot,
      lineStyle: { color: 'rgba(41, 98, 255, 0.5)', type: 'dashed', width: 1 },
      label: { formatter: `S=${props.params.spot}`, color: '#2962ff', fontSize: 10 }
    })
  }

  if (series.length > 0 && markLines.length > 0) {
    (series[0] as any).markLine = { silent: true, symbol: 'none', data: markLines }
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
      type: 'value',
      name: '标的价格',
      nameTextStyle: { color: '#8899bb' },
      axisLine: { lineStyle: { color: 'rgba(74, 85, 104, 0.3)' } },
      axisLabel: { color: '#8899bb' },
      splitLine: { lineStyle: { color: 'rgba(74, 85, 104, 0.1)' } },
    },
    yAxis: {
      type: 'value',
      name: '盈亏',
      nameTextStyle: { color: '#8899bb' },
      axisLine: { lineStyle: { color: 'rgba(74, 85, 104, 0.3)' } },
      axisLabel: { color: '#8899bb' },
      splitLine: { lineStyle: { color: 'rgba(74, 85, 104, 0.1)' } },
    },
    series,
  }, true)
}

function handleResize() { chart?.resize() }

onMounted(() => {
  if (chartRef.value) {
    chart = echarts.init(chartRef.value)
    render()
  }
  window.addEventListener('resize', handleResize)
})

onUnmounted(() => {
  window.removeEventListener('resize', handleResize)
  chart?.dispose()
})

watch(() => [props.result, props.multiResults], render, { deep: true })
</script>

<style scoped>
.payoff-chart { height: 100%; }
.chart-area { width: 100%; height: 100%; min-height: 400px; }
</style>
