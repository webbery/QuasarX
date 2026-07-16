<template>
  <div class="participation-chart">
    <div ref="chartRef" class="chart-container"></div>
  </div>
</template>

<script setup lang="ts">
import { ref, watch, onMounted, onUnmounted } from 'vue'
import * as echarts from 'echarts'

interface CapacityPoint {
  capital: number
  avg_participation: number
  max_participation: number
  avg_slippage_bps: number
  orders_above_limit: number
}

const props = defineProps<{
  curve: CapacityPoint[]
  maxParticipation: number
}>()

const chartRef = ref<HTMLDivElement>()
let chart: echarts.ECharts | null = null

function renderChart() {
  if (!chartRef.value || !props.curve.length) return

  if (!chart) {
    chart = echarts.init(chartRef.value)
  }

  const capitals = props.curve.map(p => p.capital)
  const avgParts = props.curve.map(p => p.avg_participation * 100)
  const maxParts = props.curve.map(p => p.max_participation * 100)
  const slippageBps = props.curve.map(p => p.avg_slippage_bps)

  chart.setOption({
    tooltip: {
      trigger: 'axis',
      formatter: (params: any) => {
        const idx = params[0]?.dataIndex ?? 0
        const p = props.curve[idx]
        if (!p) return ''
        return `资金: ${formatCap(p.capital)}<br/>
                平均参与率: ${(p.avg_participation * 100).toFixed(3)}%<br/>
                最大参与率: ${(p.max_participation * 100).toFixed(3)}%<br/>
                平均冲击: ${p.avg_slippage_bps.toFixed(1)} bps<br/>
                超限订单: ${p.orders_above_limit}`
      }
    },
    legend: { data: ['平均参与率', '最大参与率', '冲击成本(bps)'], textStyle: { color: '#aaa' }, top: 4 },
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
        name: '参与率(%)',
        nameTextStyle: { color: '#ce93d8' },
        axisLabel: { color: '#ce93d8', formatter: '{value}%' },
        splitLine: { lineStyle: { color: '#2a2a3e' } }
      },
      {
        type: 'value',
        name: '冲击(bps)',
        nameTextStyle: { color: '#ffab91' },
        axisLabel: { color: '#ffab91' },
        splitLine: { show: false }
      }
    ],
    series: [
      {
        name: '平均参与率',
        type: 'bar',
        data: avgParts,
        yAxisIndex: 0,
        itemStyle: { color: 'rgba(206,147,216,0.6)' },
        barMaxWidth: 20
      },
      {
        name: '最大参与率',
        type: 'line',
        data: maxParts,
        yAxisIndex: 0,
        lineStyle: { color: '#ce93d8', width: 1, type: 'dashed' },
        itemStyle: { color: '#ce93d8' },
        symbol: 'circle',
        symbolSize: 4
      },
      {
        name: '冲击成本(bps)',
        type: 'line',
        data: slippageBps,
        yAxisIndex: 1,
        lineStyle: { color: '#ffab91', width: 2 },
        itemStyle: { color: '#ffab91' },
        symbol: 'circle',
        symbolSize: 4
      },
      // 参与率上限线
      {
        type: 'line',
        markLine: {
          silent: true,
          symbol: 'none',
          lineStyle: { color: '#ef5350', type: 'dashed', width: 1 },
          label: { formatter: `${(props.maxParticipation * 100).toFixed(0)}%上限`, color: '#ef5350', fontSize: 10 },
          data: [{ yAxis: props.maxParticipation * 100 }]
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
.participation-chart { width: 100%; }
.chart-container { width: 100%; height: 300px; }
</style>
