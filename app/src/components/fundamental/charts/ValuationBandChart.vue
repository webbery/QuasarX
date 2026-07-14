<template>
  <div class="valuation-band-chart" ref="chartRef" style="width: 100%; height: 100%"></div>
</template>

<script setup lang="ts">
import { watch, onMounted } from 'vue'
import { useECharts, createBaseChartOption } from '../../report/composables/useECharts'
import type { AlignedData } from '../composables/useFundamentalState'

const props = defineProps<{
  data: AlignedData | null
  bands: [number, number, number]  // [p25, p50, p75]
}>()

const { chartRef, initChart, updateChart } = useECharts(false)
onMounted(() => initChart())

watch(() => [props.data, props.bands] as const, () => {
  if (!props.data) return

  const { dates, close, pe, pePercentiles } = props.data
  const { p25, p50, p75 } = pePercentiles

  // 估值带：根据 PE 所处区间着色背景
  // 低于 p25 → 绿色，p25~p75 → 灰色，高于 p75 → 红色
  const bandData = pe.map((v, i) => {
    if (v == null || v <= 0) return null
    if (v <= p25) return { value: close[i], itemStyle: { color: 'rgba(0,200,83,0.3)' } }
    if (v >= p75) return { value: close[i], itemStyle: { color: 'rgba(255,61,0,0.3)' } }
    return close[i]
  })

  const option = createBaseChartOption({
    title: { text: '估值带 (PE 分位)', left: 'center', textStyle: { color: '#e0e0e0', fontSize: 13 } },
    tooltip: {
      trigger: 'axis',
      formatter: (params: any[]) => {
        const idx = params[0]?.dataIndex
        let tip = params[0]?.axisValue || ''
        tip += `<br/>股价: ${close[idx]?.toFixed(2)}`
        if (pe[idx] != null) tip += `<br/>PE: ${pe[idx]?.toFixed(1)}`
        tip += `<br/>P25: ${p25.toFixed(1)} | P50: ${p50.toFixed(1)} | P75: ${p75.toFixed(1)}`
        return tip
      },
    },
    legend: { data: ['股价', `P25=${p25.toFixed(0)}`, `P50=${p50.toFixed(0)}`, `P75=${p75.toFixed(0)}`], top: 25, textStyle: { color: '#a0aec0', fontSize: 10 } },
    grid: { left: 60, right: 20, top: 60, bottom: 40 },
    xAxis: {
      type: 'category',
      data: dates,
      axisLabel: { color: '#a0aec0', fontSize: 10 },
    },
    yAxis: {
      type: 'value',
      name: '股价',
      axisLabel: { color: '#a0aec0' },
      splitLine: { lineStyle: { color: '#333' } },
    },
    dataZoom: [{ type: 'inside' }, { type: 'slider', height: 20, bottom: 5 }],
    series: [
      {
        name: '股价',
        type: 'line',
        data: bandData as any,
        symbol: 'none',
        lineStyle: { width: 1.5, color: '#2962ff' },
        areaStyle: { color: 'rgba(41,98,255,0.05)' },
      },
      {
        name: `P25=${p25.toFixed(0)}`,
        type: 'line',
        data: dates.map(() => p25),
        symbol: 'none',
        lineStyle: { width: 1, type: 'dashed', color: '#00c853' },
      },
      {
        name: `P50=${p50.toFixed(0)}`,
        type: 'line',
        data: dates.map(() => p50),
        symbol: 'none',
        lineStyle: { width: 1, type: 'dashed', color: '#ff9800' },
      },
      {
        name: `P75=${p75.toFixed(0)}`,
        type: 'line',
        data: dates.map(() => p75),
        symbol: 'none',
        lineStyle: { width: 1, type: 'dashed', color: '#ff3d00' },
      },
    ],
  })

  updateChart(option)
}, { immediate: true, deep: true })
</script>
