<template>
  <div class="chart-container">
    <div class="chart-header">
      <select v-model="selectedSymbol" class="symbol-select">
        <option value="__all__">全部标的</option>
        <option v-for="sym in symbolList" :key="sym" :value="sym">{{ sym }}</option>
      </select>
    </div>
    <div ref="chartRef" class="chart"></div>
  </div>
</template>

<script setup lang="ts">
import { ref, onMounted, watch, nextTick, computed } from 'vue'
import * as echarts from 'echarts'

interface CusumResult {
  symbol: string
  s_pos: number[]
  s_neg: number[]
  threshold: number
  change_points: number[]
}

interface Props {
  results: CusumResult[]
  dates: string[]
}

const props = defineProps<Props>()
const chartRef = ref<HTMLElement>()
let chartInstance: echarts.ECharts | null = null

const selectedSymbol = ref('__all__')
const symbolList = computed(() => props.results.map(r => r.symbol))

function renderChart() {
  if (!chartInstance || !props.results.length) return

  const filtered = selectedSymbol.value === '__all__'
    ? props.results
    : props.results.filter(r => r.symbol === selectedSymbol.value)

  if (filtered.length === 0) return

  const n = filtered[0]?.s_pos.length || 0
  if (n === 0) return

  const xData = props.dates.length > n
    ? props.dates.slice(1).slice(0, n)
    : props.dates.length === n
      ? props.dates
      : Array.from({ length: n }, (_, i) => `Day ${i + 1}`)

  const series: any[] = []
  const legendData: string[] = []

  for (const res of filtered) {
    if (res.s_pos.length !== n) continue

    const lineStyle = filtered.length === 1
      ? undefined
      : { color: '#ffffff', width: 3, type: 'solid' as const }

    const sPosName = `${res.symbol} - S+`
    legendData.push(sPosName)
    series.push({
      name: sPosName,
      type: 'line',
      data: res.s_pos,
      smooth: true,
      symbol: 'none',
      lineStyle: lineStyle || { color: '#ff9800', width: 1.5 },
      tooltip: { show: false },
    })

    const sNegName = `${res.symbol} - S-`
    legendData.push(sNegName)
    series.push({
      name: sNegName,
      type: 'line',
      data: res.s_neg,
      smooth: true,
      symbol: 'none',
      lineStyle: lineStyle || { color: '#9c27b0', width: 1.5 },
      tooltip: { show: false },
    })

    if (filtered.length === 1) {
      legendData.push('Threshold (控制限)')
      series.push({
        name: 'Threshold (控制限)',
        type: 'line',
        data: Array(n).fill(res.threshold),
        symbol: 'none',
        lineStyle: { color: '#ff1744', width: 2, type: 'dashed' },
        tooltip: { show: false },
      })
    }

    if (filtered.length === 1 && res.change_points.length > 0) {
      legendData.push('Change Points (变点)')
      series.push({
        name: 'Change Points (变点)',
        type: 'scatter',
        data: res.change_points.map(idx => ({
          value: [idx, res.s_pos[idx] > res.s_neg[idx] ? res.s_pos[idx] : res.s_neg[idx]],
        })),
        symbol: 'triangle',
        symbolSize: 12,
        itemStyle: { color: '#ff1744' },
        tooltip: { formatter: (p: any) => `⚠ 变点: ${xData[p.data[0]]}` },
      })
    }
  }

  const option = {
    tooltip: { trigger: 'axis' },
    legend: {
      data: legendData,
      top: 5,
      textStyle: { color: '#999', fontSize: 10 },
      type: 'scroll',
    },
    grid: { left: '3%', right: '4%', bottom: '3%', containLabel: true },
    xAxis: { type: 'category', data: xData, axisLabel: { rotate: 45, fontSize: 11 } },
    yAxis: { type: 'value', name: 'CUSUM Statistic (Variance)', splitLine: { lineStyle: { color: '#2a3449' } } },
    dataZoom: [{ type: 'inside' }, { type: 'slider', height: 20, bottom: 10 }],
    series,
  }

  chartInstance.setOption(option, true)
}

onMounted(async () => {
  if (chartRef.value) {
    chartInstance = echarts.init(chartRef.value)
    await nextTick()
    renderChart()
  }
})

watch(() => props.results, () => {
  selectedSymbol.value = '__all__'
  nextTick(() => {
    requestAnimationFrame(() => {
      renderChart()
    })
  })
}, { deep: true })
watch(selectedSymbol, () => {
  requestAnimationFrame(() => {
    renderChart()
  })
})
</script>

<style scoped>
.chart-container {
  padding: 16px;
  min-height: 400px;
  height: 400px;
  display: flex;
  flex-direction: column;
}
.chart-header {
  display: flex;
  justify-content: flex-end;
  margin-bottom: 8px;
}
.symbol-select {
  padding: 4px 8px;
  background: rgba(26, 34, 54, 0.8);
  border: 1px solid rgba(74, 85, 104, 0.3);
  border-radius: 4px;
  color: #e0e0e0;
  font-size: 12px;
  outline: none;
  cursor: pointer;
}
.symbol-select:focus {
  border-color: rgba(41, 98, 255, 0.5);
}
.symbol-select option {
  background: #1a2236;
  color: #e0e0e0;
}
.chart {
  flex: 1;
  width: 100%;
}
</style>
