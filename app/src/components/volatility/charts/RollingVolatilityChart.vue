<template>
  <div ref="chartRef" style="width: 100%; height: 100%"></div>
</template>

<script setup lang="ts">
import { watch, onMounted } from 'vue'
import * as echarts from 'echarts'
import { useECharts, createBaseChartOption } from '../../report/composables/useECharts'
import type { VolatilitySingleResult } from '../composables/useVolatilityState'

const props = defineProps<{
  data: VolatilitySingleResult | null
  windows?: number[]
  dates?: string[]
}>()

const { chartRef, initChart, updateChart } = useECharts(false) // 不自动初始化

function buildOption() {
  if (!props.data?.rolling_vol) return {}

  const rollingVol = props.data.rolling_vol
  const windows = props.windows || [20, 60, 120]
  const colors = ['#2962ff', '#ff9800', '#00c853']

  // 找到所有窗口中最短的长度，确保数据末尾对齐
  const minLen = Math.min(...windows.map(w => rollingVol[w]?.length || 0))
  
  // 按末尾对齐：取每个窗口最后 minLen 个数据点
  const series = windows.map((w, i) => ({
    name: `${w}日滚动`,
    type: 'line' as const,
    data: rollingVol[w]?.slice(-minLen).map((v: number) => (v * 100).toFixed(2)) || [],
    lineStyle: { color: colors[i], width: 2 },
    showSymbol: false,
    smooth: true
  }))

  // 使用 dates 的最后 minLen 个作为 x 轴标签
  const dates = props.dates || []
  const displayDates = dates.length > 0 ? dates.slice(-minLen) : Array.from({ length: minLen }, (_, i) => i)

  return createBaseChartOption({
    title: {
      text: '滚动波动率',
      left: 'center',
      textStyle: { color: '#e0e0e0', fontSize: 14 }
    },
    tooltip: { trigger: 'axis' },
    legend: { data: windows.map(w => `${w}日滚动`), top: 25, textStyle: { color: '#999' } },
    grid: { left: '3%', right: '4%', bottom: '3%', top: '20%', containLabel: true },
    xAxis: {
      type: 'category',
      data: displayDates,
      axisLabel: {
        color: '#999',
        interval: Math.max(0, Math.floor(minLen / 10) - 1),
        rotate: 30,
        formatter: (val: string) => {
          if (typeof val === 'string' && val.includes('-')) {
            const datePart = val.split(' ')[0]
            const parts = datePart.split('-')
            if (parts.length === 3) return `${parts[1]}-${parts[2]}`
          }
          return val
        }
      }
    },
    yAxis: {
      type: 'value',
      name: '年化波动率 (%)',
      axisLabel: { color: '#999' },
      nameTextStyle: { color: '#999' }
    },
    series
  })
}

watch(() => props.data, (newData) => {
  if (newData && chartRef.value) {
    if (!echarts.getInstanceByDom(chartRef.value)) {
      initChart()
    }
    updateChart(buildOption(), true)
  }
}, { immediate: true })

// 组件挂载后确保图表已初始化
onMounted(() => {
  if (chartRef.value && props.data && !echarts.getInstanceByDom(chartRef.value)) {
    initChart()
    updateChart(buildOption(), true)
  }
})
</script>
