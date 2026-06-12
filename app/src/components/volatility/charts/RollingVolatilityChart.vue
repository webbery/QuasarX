<template>
  <div ref="chartRef" style="width: 100%; height: 100%"></div>
</template>

<script setup lang="ts">
import { watch } from 'vue'
import * as echarts from 'echarts'
import { useECharts, createBaseChartOption } from '../../report/composables/useECharts'
import type { VolatilitySingleResult } from '../composables/useVolatilityState'

const props = defineProps<{
  data: VolatilitySingleResult | null
  windows?: number[]
}>()

const { chartRef, initChart, updateChart } = useECharts()

function buildOption() {
  if (!props.data?.rolling_vol) return {}
  
  const rollingVol = props.data.rolling_vol
  const windows = props.windows || [20, 60, 120]
  const colors = ['#2962ff', '#ff9800', '#00c853']
  
  const series = windows.map((w, i) => ({
    name: `${w}日滚动`,
    type: 'line' as const,
    data: rollingVol[w]?.map((v: number) => (v * 100).toFixed(2)) || [],
    lineStyle: { color: colors[i], width: 2 },
    showSymbol: false,
    smooth: true
  }))
  
  // 对齐到 dates（滚动波动率比原始数据短）
  const maxLen = Math.max(...windows.map(w => rollingVol[w]?.length || 0))
  const offset = (props.data.prices.length - 1) - maxLen
  
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
      data: props.data.prices.slice(offset + 1).map((_, i) => i),
      axisLabel: { color: '#999', interval: Math.floor(maxLen / 10) }
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

watch(() => props.data, () => {
  if (props.data) {
    if (chartRef.value && !echarts.getInstanceByDom(chartRef.value)) initChart()
    updateChart(buildOption(), true)
  }
}, { immediate: true })
</script>
