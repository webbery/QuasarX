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
}>()

const { chartRef, initChart, updateChart } = useECharts()

function buildOption() {
  if (!props.data?.returns) return {}
  
  const absReturns = props.data.abs_returns
  const volumes = props.data.volumes
  
  // 散点数据: x=成交量, y=|收益率|, 颜色=时间索引
  const scatterData = absReturns.map((r, i) => [volumes[i] || 0, r, i])
  
  return createBaseChartOption({
    title: {
      text: '波动率聚集分析',
      left: 'center',
      textStyle: { color: '#e0e0e0', fontSize: 14 }
    },
    tooltip: {
      trigger: 'item',
      formatter: (p: any) => {
        const [vol, absRet, idx] = p.data
        return `交易日 #${idx}<br/>成交量: ${vol}<br/>|收益率|: ${(absRet * 100).toFixed(3)}%`
      }
    },
    grid: { left: '3%', right: '8%', bottom: '8%', top: '15%', containLabel: true },
    xAxis: {
      type: 'value',
      name: '成交量',
      axisLabel: { color: '#999' },
      nameTextStyle: { color: '#999' }
    },
    yAxis: {
      type: 'value',
      name: '|收益率|',
      axisLabel: { color: '#999', formatter: (v: number) => (v * 100).toFixed(2) + '%' },
      nameTextStyle: { color: '#999' }
    },
    visualMap: {
      show: true,
      dimension: 2,
      min: 0,
      max: absReturns.length,
      orient: 'horizontal',
      left: 'center',
      bottom: 0,
      text: ['早期', '近期'],
      calculable: true,
      inRange: {
        color: ['#2962ff', '#ff9800', '#ef232a']
      },
      textStyle: { color: '#999' }
    },
    series: [{
      name: '波动率聚集',
      type: 'scatter',
      data: scatterData,
      symbolSize: 6,
      itemStyle: { opacity: 0.7 }
    }]
  })
}

watch(() => props.data, () => {
  if (props.data) {
    if (chartRef.value && !echarts.getInstanceByDom(chartRef.value)) initChart()
    updateChart(buildOption(), true)
  }
}, { immediate: true })
</script>
