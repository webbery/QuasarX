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

function buildACFOption(values: number[], title: string) {
  const n = values.length
  // 95% 置信区间
  const ci = 1.96 / Math.sqrt(n)
  
  return createBaseChartOption({
    title: { text: title, left: 'center', textStyle: { color: '#e0e0e0', fontSize: 12 } },
    tooltip: { trigger: 'axis' },
    grid: { left: '3%', right: '4%', bottom: '8%', top: '15%', containLabel: true },
    xAxis: {
      type: 'category',
      data: Array.from({ length: n }, (_, i) => i),
      axisLabel: { color: '#999', interval: Math.floor(n / 10) }
    },
    yAxis: {
      type: 'value',
      min: -1,
      max: 1,
      axisLabel: { color: '#999' }
    },
    series: [{
      type: 'bar',
      data: values,
      itemStyle: {
        color: (p: any) => Math.abs(p.value) > ci ? '#2962ff' : 'rgba(160, 174, 192, 0.5)'
      }
    }],
    graphic: [{
      type: 'line',
      shape: { x1: 0, y1: ci * 100 + '%', x2: '100%', y2: ci * 100 + '%' },
      style: { stroke: '#ef232a', lineWidth: 1, lineDash: [4, 4] }
    }, {
      type: 'line',
      shape: { x1: 0, y1: -ci * 100 + '%', x2: '100%', y2: -ci * 100 + '%' },
      style: { stroke: '#ef232a', lineWidth: 1, lineDash: [4, 4] }
    }]
  })
}

function buildOption() {
  if (!props.data?.returns_acf) return {}
  // 返回两个图表的配置（通过外部组件分别渲染）
  return { acf: props.data.returns_acf, pacf: props.data.returns_pacf, absAcf: props.data.abs_returns_acf }
}

// 这里只渲染 |r| ACF（波动率聚集信号）
watch(() => props.data, () => {
  if (props.data?.abs_returns_acf) {
    if (chartRef.value && !echarts.getInstanceByDom(chartRef.value)) initChart()
    const ci = 1.96 / Math.sqrt(props.data.returns.length)
    
    const option = createBaseChartOption({
      title: { text: '|收益率| ACF（波动率聚集信号）', left: 'center', textStyle: { color: '#e0e0e0', fontSize: 13 } },
      tooltip: { trigger: 'axis', formatter: (p: any) => `Lag ${p[0].name}<br/>ACF: ${p[0].value.toFixed(4)}` },
      grid: { left: '3%', right: '4%', bottom: '8%', top: '18%', containLabel: true },
      xAxis: {
        type: 'category',
        data: Array.from({ length: props.data.abs_returns_acf.length }, (_, i) => i),
        axisLabel: { color: '#999' }
      },
      yAxis: { type: 'value', min: -1, max: 1, axisLabel: { color: '#999' } },
      series: [{
        type: 'bar',
        data: props.data.abs_returns_acf,
        itemStyle: {
          color: (p: any) => Math.abs(p.value) > ci ? '#ff9800' : 'rgba(160, 174, 192, 0.4)'
        }
      }],
      graphic: [{
        type: 'line',
        left: 0, right: 0,
        shape: { x1: 0, y1: ci * 100 + '%', x2: '100%', y2: ci * 100 + '%' },
        style: { stroke: '#ef232a', lineWidth: 1, lineDash: [4, 4] }
      }, {
        type: 'line',
        left: 0, right: 0,
        shape: { x1: 0, y1: -ci * 100 + '%', x2: '100%', y2: -ci * 100 + '%' },
        style: { stroke: '#ef232a', lineWidth: 1, lineDash: [4, 4] }
      }]
    })
    
    updateChart(option, true)
  }
}, { immediate: true })
</script>
