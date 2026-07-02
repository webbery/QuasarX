<template>
  <div class="chart-container">
    <div ref="chartRef" class="chart"></div>
  </div>
</template>

<script setup lang="ts">
import { ref, onMounted, watch, nextTick } from 'vue'
import * as echarts from 'echarts'

interface Props {
  sPos: number[]
  sNeg: number[]
  threshold: number
  changePoints: number[]
  dates: string[]
}

const props = defineProps<Props>()
const chartRef = ref<HTMLElement>()
let chartInstance: echarts.ECharts | null = null

function renderChart() {
  if (!chartInstance || !props.sPos.length) return

  const n = props.sPos.length
  const xData = props.dates.length === n ? props.dates : Array.from({ length: n }, (_, i) => `Day ${i + 1}`)

  const option = {
    tooltip: {
      trigger: 'axis',
      formatter: (params: any) => {
        const day = params[0].axisValue
        const sPosVal = params[0].data.toFixed(4)
        const sNegVal = params[1].data.toFixed(4)
        const isChange = props.changePoints.includes(params[0].dataIndex)
        return `${day}<br/>S+: ${sPosVal}<br/>S-: ${sNegVal}${isChange ? '<br/><span style="color:#ff1744">⚠ 变点触发</span>' : ''}`
      },
    },
    grid: { left: '3%', right: '4%', bottom: '3%', containLabel: true },
    xAxis: {
      type: 'category',
      data: xData,
      axisLabel: { rotate: 45, fontSize: 11 },
    },
    yAxis: {
      type: 'value',
      name: 'CUSUM Statistic',
      splitLine: { lineStyle: { color: '#2a3449' } },
    },
    dataZoom: [{ type: 'inside' }, { type: 'slider', height: 20, bottom: 10 }],
    series: [
      {
        name: 'S+',
        type: 'line',
        data: props.sPos,
        smooth: true,
        symbol: 'none',
        lineStyle: { color: '#ef232a', width: 2 },
        areaStyle: { color: 'rgba(239, 35, 42, 0.1)' },
      },
      {
        name: 'S-',
        type: 'line',
        data: props.sNeg,
        smooth: true,
        symbol: 'none',
        lineStyle: { color: '#2962ff', width: 2 },
        areaStyle: { color: 'rgba(41, 98, 255, 0.1)' },
      },
      {
        name: 'Threshold',
        type: 'line',
        data: Array(n).fill(props.threshold),
        symbol: 'none',
        lineStyle: { color: '#ff9800', width: 2, type: 'dashed' },
        tooltip: { show: false },
      },
      {
        name: 'Change Points',
        type: 'scatter',
        data: props.changePoints.map(idx => ({
          value: [idx, props.sPos[idx] > props.sNeg[idx] ? props.sPos[idx] : props.sNeg[idx]],
        })),
        symbol: 'triangle',
        symbolSize: 12,
        itemStyle: { color: '#ff1744' },
        tooltip: { formatter: (p: any) => `⚠ 变点: ${xData[p.data[0]]}` },
      },
    ],
  }

  chartInstance.setOption(option)
}

onMounted(async () => {
  if (chartRef.value) {
    chartInstance = echarts.init(chartRef.value)
    await nextTick()
    renderChart()
  }
})

watch(() => [props.sPos, props.sNeg, props.threshold, props.changePoints], renderChart, { deep: true })

window.addEventListener('resize', () => chartInstance?.resize())
</script>

<style scoped>
.chart-container {
  padding: 16px;
  height: 350px;
}
.chart {
  width: 100%;
  height: 100%;
}
</style>
