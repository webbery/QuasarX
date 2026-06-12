<template>
  <div ref="chartRef" style="width: 100%; height: 100%"></div>
</template>

<script setup lang="ts">
import { watch } from 'vue'
import * as echarts from 'echarts'
import { useECharts, createBaseChartOption } from '../../report/composables/useECharts'

const props = defineProps<{
  symbols: string[]
  correlationMatrix: number[][]
}>()

const { chartRef, initChart, updateChart } = useECharts()

function buildOption() {
  const { symbols, correlationMatrix } = props
  const n = symbols.length
  if (n === 0 || !correlationMatrix) return {}
  
  // 构建热力图数据
  const data: number[][] = []
  for (let i = 0; i < n; i++) {
    for (let j = 0; j < n; j++) {
      data.push([j, i, correlationMatrix[i][j]])
    }
  }
  
  return createBaseChartOption({
    title: {
      text: '相关性热力图',
      left: 'center',
      textStyle: { color: '#e0e0e0', fontSize: 14 }
    },
    tooltip: {
      position: 'top',
      formatter: (p: any) => {
        const [x, y, v] = p.data
        return `${symbols[y]} ↔ ${symbols[x]}<br/>相关系数: ${v.toFixed(3)}`
      }
    },
    grid: { height: '70%', top: '15%' },
    xAxis: {
      type: 'category',
      data: symbols.map(s => s.split('.')[1] || s),
      axisLabel: { color: '#999', rotate: 30 },
      splitArea: { show: true }
    },
    yAxis: {
      type: 'category',
      data: symbols.map(s => s.split('.')[1] || s),
      axisLabel: { color: '#999' },
      splitArea: { show: true }
    },
    visualMap: {
      min: -1,
      max: 1,
      calculable: true,
      orient: 'horizontal',
      left: 'center',
      bottom: '5%',
      text: ['高', '低'],
      inRange: {
        color: ['#ef232a', '#1a2236', '#00c853']
      },
      textStyle: { color: '#999' }
    },
    series: [{
      name: '相关系数',
      type: 'heatmap',
      data,
      label: {
        show: true,
        formatter: (p: any) => p.data[2].toFixed(2),
        fontSize: 10,
        color: '#e0e0e0'
      },
      emphasis: {
        itemStyle: { shadowBlur: 10, shadowColor: 'rgba(0, 0, 0, 0.5)' }
      }
    }]
  })
}

watch(() => [props.symbols, props.correlationMatrix], () => {
  if (props.symbols.length >= 2 && props.correlationMatrix) {
    if (chartRef.value && !echarts.getInstanceByDom(chartRef.value)) initChart()
    updateChart(buildOption(), true)
  }
}, { immediate: true })
</script>
