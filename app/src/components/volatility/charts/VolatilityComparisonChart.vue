<template>
  <div ref="chartRef" style="width: 100%; height: 100%"></div>
</template>

<script setup lang="ts">
import { watch, onMounted } from 'vue'
import * as echarts from 'echarts'
import { useECharts, createBaseChartOption } from '../../report/composables/useECharts'

const props = defineProps<{
  symbols: string[]
  annualVolatility: number[]
}>()

const { chartRef, initChart, updateChart } = useECharts(false) // 不自动初始化

function buildOption() {
  const { symbols, annualVolatility } = props
  if (!symbols.length || !annualVolatility) return {}
  
  return createBaseChartOption({
    title: {
      text: '年化波动率对比',
      left: 'center',
      textStyle: { color: '#e0e0e0', fontSize: 14 }
    },
    tooltip: { trigger: 'axis', formatter: (p: any) => `${p[0].name}<br/>年化波动率: ${(p[0].value * 100).toFixed(2)}%` },
    grid: { left: '3%', right: '4%', bottom: '15%', top: '15%', containLabel: true },
    xAxis: {
      type: 'category',
      data: symbols.map(s => s.split('.')[0] || s),
      axisLabel: {
        color: '#999',
        rotate: 45,
        interval: 0,
        fontSize: 11
      }
    },
    yAxis: {
      type: 'value',
      name: '年化波动率 (%)',
      axisLabel: { color: '#999', formatter: (v: number) => (v * 100).toFixed(0) + '%' },
      nameTextStyle: { color: '#999' }
    },
    series: [{
      name: '年化波动率',
      type: 'bar',
      data: annualVolatility.map((v, i) => ({
        value: v,
        itemStyle: {
          color: v > 0.4 ? '#ef232a' : v > 0.25 ? '#ff9800' : '#00c853'
        }
      })),
      label: {
        show: true,
        position: 'top',
        formatter: (p: any) => (p.value * 100).toFixed(1) + '%'
      }
    }]
  })
}

watch(() => [props.symbols, props.annualVolatility], () => {
  if (props.symbols.length >= 2 && props.annualVolatility && chartRef.value) {
    if (!echarts.getInstanceByDom(chartRef.value)) initChart()
    updateChart(buildOption(), true)
  }
}, { immediate: true })

onMounted(() => {
  if (chartRef.value && props.symbols.length >= 2 && props.annualVolatility && !echarts.getInstanceByDom(chartRef.value)) {
    initChart()
    updateChart(buildOption(), true)
  }
})
</script>
