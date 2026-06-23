<template>
  <div class="lead-lag-heatmap">
    <div class="chart-title">领先-滞后分析 (交叉相关)</div>
    <div ref="chartRef" class="chart-container" />
  </div>
</template>

<script setup lang="ts">
import { ref, onMounted, watch } from 'vue'
import * as echarts from 'echarts'

interface LeadLagItem {
  symbol_x: string
  symbol_y: string
  lead_lag: number
  max_correlation: number
  ccf: number[]
}

const props = defineProps<{
  data: LeadLagItem[]
}>()

const chartRef = ref<HTMLElement | null>(null)
let chart: echarts.ECharts | null = null

function buildChart() {
  if (!chartRef.value || !props.data.length) return

  if (!chart) {
    chart = echarts.init(chartRef.value, 'dark')
  }

  const maxLag = Math.max(...props.data.map(d => Math.ceil((d.ccf.length - 1) / 2)))
  const lags = Array.from({ length: 2 * maxLag + 1 }, (_, i) => i - maxLag)

  const xLabels = props.data.map(d => `${d.symbol_x} ↔ ${d.symbol_y}`)
  const heatmapData: [number, number, number][] = []

  props.data.forEach((item, xi) => {
    item.ccf.forEach((val, yi) => {
      heatmapData.push([xi, yi, val])
    })
  })

  chart.setOption({
    tooltip: {
      position: 'top',
      formatter: (params: any) => {
        const [x, y, val] = params.data
        return `${xLabels[x]}<br/>滞后: ${lags[y]}<br/>相关系数: ${val.toFixed(4)}`
      }
    },
    grid: { top: 30, bottom: 80, left: 100, right: 30 },
    xAxis: {
      type: 'category',
      data: xLabels,
      axisLabel: {
        rotate: 30,
        fontSize: 11,
        color: '#999'
      }
    },
    yAxis: {
      type: 'category',
      data: lags,
      name: '滞后阶数',
      nameTextStyle: { color: '#999' },
      axisLabel: { color: '#999' }
    },
    visualMap: {
      min: -1,
      max: 1,
      calculable: true,
      orient: 'horizontal',
      left: 'center',
      bottom: 0,
      inRange: {
        color: ['#2166ac', '#4393c3', '#92c5de', '#f7f7f7', '#fddbc7', '#f4a582', '#d6604d']
      },
      textStyle: { color: '#999' }
    },
    series: [{
      type: 'heatmap',
      data: heatmapData,
      label: { show: false },
      emphasis: {
        itemStyle: {
          shadowBlur: 10,
          shadowColor: 'rgba(0, 0, 0, 0.5)'
        }
      }
    }]
  })
}

onMounted(() => {
  buildChart()
  window.addEventListener('resize', () => chart?.resize())
})

watch(() => props.data, buildChart, { deep: true })
</script>

<style scoped>
.lead-lag-heatmap {
  height: 100%;
  display: flex;
  flex-direction: column;
}

.chart-title {
  font-size: 14px;
  font-weight: 600;
  color: #e0e0e0;
  margin-bottom: 12px;
  padding-left: 8px;
  border-left: 3px solid #2962ff;
}

.chart-container {
  flex: 1;
  min-height: 0;
}
</style>
