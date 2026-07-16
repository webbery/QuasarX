<template>
  <div class="capacity-decay-chart">
    <div ref="chartRef" class="chart-container"></div>
  </div>
</template>

<script setup lang="ts">
import { ref, watch, onMounted, onUnmounted } from 'vue'
import * as echarts from 'echarts'

interface CapacityPoint {
  capital: number
  sharpe: number
  total_return: number
  max_drawdown: number
  win_rate: number
  avg_participation: number
  max_participation: number
  avg_slippage_bps: number
  orders_above_limit: number
  sharpe_decay: number
}

const props = defineProps<{
  curve: CapacityPoint[]
  baselineSharpe: number
}>()

const chartRef = ref<HTMLDivElement>()
let chart: echarts.ECharts | null = null

function renderChart() {
  if (!chartRef.value || !props.curve.length) return

  if (!chart) {
    chart = echarts.init(chartRef.value)
  }

  const capitals = props.curve.map(p => p.capital)
  const sharpes = props.curve.map(p => p.sharpe)
  const returns = props.curve.map(p => p.total_return * 100)

  const decay20 = props.baselineSharpe * 0.8
  const decay50 = props.baselineSharpe * 0.5

  chart.setOption({
    tooltip: {
      trigger: 'axis',
      formatter: (params: any) => {
        const idx = params[0]?.dataIndex ?? 0
        const p = props.curve[idx]
        if (!p) return ''
        return `资金: ${formatCap(p.capital)}<br/>
                Sharpe: ${p.sharpe.toFixed(2)}<br/>
                总收益: ${(p.total_return * 100).toFixed(2)}%<br/>
                最大回撤: ${(p.max_drawdown * 100).toFixed(2)}%<br/>
                衰减: ${(p.sharpe_decay * 100).toFixed(1)}%`
      }
    },
    legend: { data: ['Sharpe', '总收益'], textStyle: { color: '#aaa' }, top: 4 },
    grid: { left: 60, right: 60, top: 40, bottom: 40 },
    xAxis: {
      type: 'log',
      name: '资金量',
      nameTextStyle: { color: '#888' },
      axisLabel: { color: '#aaa', formatter: (v: number) => formatCap(v) },
      splitLine: { lineStyle: { color: '#333' } }
    },
    yAxis: [
      {
        type: 'value',
        name: 'Sharpe',
        nameTextStyle: { color: '#4fc3f7' },
        axisLabel: { color: '#4fc3f7' },
        splitLine: { lineStyle: { color: '#2a2a3e' } }
      },
      {
        type: 'value',
        name: '收益(%)',
        nameTextStyle: { color: '#81c784' },
        axisLabel: { color: '#81c784', formatter: '{value}%' },
        splitLine: { show: false }
      }
    ],
    series: [
      {
        name: 'Sharpe',
        type: 'line',
        data: sharpes,
        yAxisIndex: 0,
        lineStyle: { color: '#4fc3f7', width: 2 },
        itemStyle: { color: '#4fc3f7' },
        symbol: 'circle',
        symbolSize: 5
      },
      {
        name: '总收益',
        type: 'line',
        data: returns,
        yAxisIndex: 1,
        lineStyle: { color: '#81c784', width: 2 },
        itemStyle: { color: '#81c784' },
        symbol: 'circle',
        symbolSize: 5
      },
      // 20% 衰减线
      {
        type: 'line',
        markLine: {
          silent: true,
          symbol: 'none',
          lineStyle: { color: '#ffb74d', type: 'dashed', width: 1 },
          label: { formatter: '20%衰减', color: '#ffb74d', fontSize: 10 },
          data: [{ yAxis: decay20 }]
        },
        data: []
      },
      // 50% 衰减线
      {
        type: 'line',
        markLine: {
          silent: true,
          symbol: 'none',
          lineStyle: { color: '#ef5350', type: 'dashed', width: 1 },
          label: { formatter: '50%衰减', color: '#ef5350', fontSize: 10 },
          data: [{ yAxis: decay50 }]
        },
        data: []
      }
    ]
  })
}

function formatCap(v: number): string {
  if (v >= 1e8) return (v / 1e8).toFixed(1) + '亿'
  if (v >= 1e4) return (v / 1e4).toFixed(0) + '万'
  return v.toFixed(0)
}

function handleResize() { chart?.resize() }

watch(() => props.curve, renderChart, { deep: true })
onMounted(() => {
  renderChart()
  window.addEventListener('resize', handleResize)
})
onUnmounted(() => {
  window.removeEventListener('resize', handleResize)
  chart?.dispose()
})
</script>

<style scoped>
.capacity-decay-chart { width: 100%; }
.chart-container { width: 100%; height: 320px; }
</style>
