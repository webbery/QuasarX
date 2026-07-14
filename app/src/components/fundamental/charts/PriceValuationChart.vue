<template>
  <div class="price-valuation-chart" ref="chartRef" style="width: 100%; height: 100%"></div>
</template>

<script setup lang="ts">
import { watch, onMounted } from 'vue'
import * as echarts from 'echarts'
import { useECharts, createBaseChartOption } from '../../report/composables/useECharts'
import type { AlignedData } from '../composables/useFundamentalState'

const props = defineProps<{
  data: AlignedData | null
}>()

const { chartRef, initChart, updateChart } = useECharts(false)

onMounted(() => initChart())

function buildOption() {
  if (!props.data) return {}

  const { dates, close, pe, earningsDates } = props.data

  // 财报发布日标记线
  const markLines = earningsDates.map(e => ({
    xAxis: e.date,
    label: {
      formatter: e.label,
      fontSize: 9,
      color: '#a0aec0',
      distance: 5,
    },
    lineStyle: { color: '#a0aec0', type: 'dashed' as const, width: 1 },
  }))

  return createBaseChartOption({
    title: { text: '股价 & PE', left: 'center', textStyle: { color: '#e0e0e0', fontSize: 13 } },
    tooltip: {
      trigger: 'axis',
      axisPointer: { type: 'cross' },
      formatter: (params: any[]) => {
        let tip = params[0]?.axisValue || ''
        for (const p of params) {
          if (p.seriesName === '股价') {
            tip += `<br/>股价: ${p.value?.toFixed(2)}`
          } else if (p.seriesName === 'PE' && p.value != null) {
            tip += `<br/>PE: ${p.value?.toFixed(1)}`
          }
        }
        return tip
      },
    },
    legend: { data: ['股价', 'PE'], top: 25, textStyle: { color: '#a0aec0' } },
    grid: { left: 60, right: 60, top: 60, bottom: 40 },
    xAxis: {
      type: 'category',
      data: dates,
      axisLabel: { color: '#a0aec0', fontSize: 10 },
      axisLine: { lineStyle: { color: '#333' } },
    },
    yAxis: [
      {
        type: 'value',
        name: '股价',
        nameTextStyle: { color: '#a0aec0' },
        axisLabel: { color: '#a0aec0' },
        splitLine: { lineStyle: { color: '#333' } },
      },
      {
        type: 'value',
        name: 'PE',
        nameTextStyle: { color: '#a0aec0' },
        axisLabel: { color: '#a0aec0' },
        splitLine: { show: false },
      },
    ],
    dataZoom: [
      { type: 'inside', start: 0, end: 100 },
      { type: 'slider', start: 0, end: 100, height: 20, bottom: 5 },
    ],
    series: [
      {
        name: '股价',
        type: 'line',
        data: close,
        yAxisIndex: 0,
        symbol: 'none',
        lineStyle: { width: 1.5, color: '#2962ff' },
        markLine: markLines.length > 0 ? {
          silent: true,
          symbol: 'none',
          data: markLines,
        } : undefined,
      },
      {
        name: 'PE',
        type: 'line',
        data: pe,
        yAxisIndex: 1,
        symbol: 'none',
        lineStyle: { width: 1, color: '#ff9800' },
        connectNulls: true,
      },
    ],
  })
}

watch(() => props.data, () => {
  if (props.data) updateChart(buildOption())
}, { immediate: true, deep: true })
</script>
