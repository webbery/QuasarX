<template>
  <div class="profitability-chart" ref="chartRef" style="width: 100%; height: 100%"></div>
</template>

<script setup lang="ts">
import { watch, onMounted } from 'vue'
import { useECharts, createBaseChartOption } from '../../report/composables/useECharts'
import type { FinanceRow } from '../composables/useFundamentalState'

const props = defineProps<{
  data: FinanceRow[]
}>()

const { chartRef, initChart, updateChart } = useECharts(false)
onMounted(() => initChart())

watch(() => props.data, () => {
  if (!props.data || props.data.length === 0) return

  const sorted = [...props.data].sort((a, b) => a.stat_date.localeCompare(b.stat_date))
  const dates = sorted.map(r => r.stat_date)

  const option = createBaseChartOption({
    title: { text: '盈利能力', left: 'center', textStyle: { color: '#e0e0e0', fontSize: 13 } },
    tooltip: { trigger: 'axis' },
    legend: { data: ['ROE', '净利率', '毛利率'], top: 25, textStyle: { color: '#a0aec0' } },
    grid: { left: 50, right: 20, top: 60, bottom: 30 },
    xAxis: {
      type: 'category',
      data: dates,
      axisLabel: { color: '#a0aec0', fontSize: 10, rotate: 30 },
    },
    yAxis: {
      type: 'value',
      name: '%',
      axisLabel: { color: '#a0aec0', formatter: (v: number) => (v * 100).toFixed(0) + '%' },
      splitLine: { lineStyle: { color: '#333' } },
    },
    series: [
      {
        name: 'ROE',
        type: 'line',
        data: sorted.map(r => r.roe_avg),
        symbol: 'circle',
        symbolSize: 5,
        lineStyle: { color: '#2962ff' },
        itemStyle: { color: '#2962ff' },
      },
      {
        name: '净利率',
        type: 'line',
        data: sorted.map(r => r.np_margin),
        symbol: 'circle',
        symbolSize: 5,
        lineStyle: { color: '#ff9800' },
        itemStyle: { color: '#ff9800' },
      },
      {
        name: '毛利率',
        type: 'line',
        data: sorted.map(r => r.gp_margin),
        symbol: 'circle',
        symbolSize: 5,
        lineStyle: { color: '#00c853' },
        itemStyle: { color: '#00c853' },
      },
    ],
  })

  updateChart(option)
}, { immediate: true, deep: true })
</script>
