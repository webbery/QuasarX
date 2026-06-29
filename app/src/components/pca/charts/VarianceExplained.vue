<template>
  <div class="variance-explained">
    <h4>方差解释比分析</h4>
    <div ref="chartRef" class="chart-container"></div>
  </div>
</template>

<script setup lang="ts">
import { ref, watch, onMounted, onBeforeUnmount } from 'vue'
import * as echarts from 'echarts'

const props = defineProps<{
  eigenvalues: number[]
  variance_ratio: number[]
  cumulative_variance: number[]
  selectedK?: number
}>()

const chartRef = ref<HTMLElement>()
let chartInstance: echarts.EChartsType | null = null

function buildOption() {
  if (!props.eigenvalues.length) return {}

  const { eigenvalues, variance_ratio, cumulative_variance, selectedK } = props
  const n = eigenvalues.length
  const indices = Array.from({ length: n }, (_, i) => `PC${i + 1}`)

  return {
    tooltip: {
      trigger: 'axis' as const,
      axisPointer: { type: 'shadow' as const }
    },
    legend: {
      data: ['方差解释比', '累计方差解释比'],
      textStyle: { color: '#e0e0e0' },
      top: 0
    },
    grid: { left: 50, right: 50, top: 40, bottom: 30 },
    xAxis: {
      type: 'category' as const,
      data: indices,
      axisLabel: { color: '#999' },
      axisLine: { lineStyle: { color: '#555' } },
      splitLine: { show: false }
    },
    yAxis: [
      {
        type: 'value' as const,
        name: '方差解释比 (%)',
        axisLabel: { color: '#999', formatter: '{value}%' },
        splitLine: { lineStyle: { color: 'rgba(74, 85, 104, 0.3)' } }
      },
      {
        type: 'value' as const,
        name: '累计 (%)',
        min: 0,
        max: 100,
        axisLabel: { color: '#999', formatter: '{value}%' },
        splitLine: { show: false }
      }
    ],
    series: [
      {
        name: '方差解释比',
        type: 'bar' as const,
        data: variance_ratio.map((v, i) => ({
          value: v * 100,
          itemStyle: {
            color: selectedK && i < selectedK ? '#2962ff' : 'rgba(41, 98, 255, 0.3)'
          }
        })),
        barMaxWidth: 40
      },
      {
        name: '累计方差解释比',
        type: 'line' as const,
        data: cumulative_variance.map(v => v * 100),
        smooth: true,
        itemStyle: { color: '#ff9800' },
        lineStyle: { width: 3 },
        yAxisIndex: 1,
        symbol: 'circle',
        symbolSize: 6,
        markLine: selectedK ? {
          data: [{ xAxis: selectedK - 1, label: { formatter: `前${selectedK}个PC` } }],
          lineStyle: { color: '#00c853', type: 'dashed' }
        } : undefined
      }
    ]
  }
}

function initChart() {
  if (!chartRef.value) return
  chartInstance = echarts.init(chartRef.value)
  chartInstance.setOption(buildOption())
}

function updateChart() {
  chartInstance?.setOption(buildOption(), true)
}

watch(() => [props.variance_ratio, props.selectedK], () => {
  if (props.variance_ratio.length > 0) {
    if (!chartInstance && chartRef.value) initChart()
    else if (chartInstance) updateChart()
  }
}, { immediate: true, deep: true })

onMounted(() => {
  window.addEventListener('resize', handleResize)
})

onBeforeUnmount(() => {
  window.removeEventListener('resize', handleResize)
  chartInstance?.dispose()
})

function handleResize() {
  chartInstance?.resize()
}
</script>

<style scoped>
.variance-explained {
  background: rgba(26, 34, 54, 0.9);
  border-radius: 8px;
  padding: 16px;
}
.chart-container {
  width: 100%;
  height: 300px;
}
</style>
