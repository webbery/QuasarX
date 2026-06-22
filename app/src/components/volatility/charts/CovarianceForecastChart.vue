<template>
  <div ref="chartRef" style="width: 100%; height: 100%"></div>
</template>

<script setup lang="ts">
import { watch, onMounted } from 'vue'
import * as echarts from 'echarts'
import { useECharts, createBaseChartOption } from '../../report/composables/useECharts'
import type { MultiForecast } from '../composables/useVolatilityState'

const props = defineProps<{
  data: MultiForecast | null
}>()

const { chartRef, initChart, updateChart } = useECharts(false)

function buildOption() {
  if (!props.data?.forecast_corr) return {}

  const { symbols, forecast_corr, forecast_cov, horizon } = props.data
  const n = symbols.length

  // 构建热力图数据
  const heatmapData: [number, number, number][] = []
  for (let i = 0; i < n; i++) {
    for (let j = 0; j < n; j++) {
      heatmapData.push([j, i, forecast_corr[i][j]])
    }
  }

  const shortNames = symbols.map(s => {
    const parts = s.split('.')
    return parts.length > 1 ? parts[0] : s
  })

  return createBaseChartOption({
    title: {
      text: `${horizon}步协方差膨胀矩阵`,
      subtext: '预测期内资产间相关系数外推',
      left: 'center',
      textStyle: { color: '#e0e0e0', fontSize: 14 },
      subtextStyle: { color: '#666', fontSize: 11 }
    },
    tooltip: {
      formatter: (p: any) => {
        const i = p.data[1], j = p.data[0]
        return `${symbols[i]} ↔ ${symbols[j]}<br/>相关系数: ${p.data[2].toFixed(3)}<br/>协方差: ${forecast_cov[i][j].toExponential(4)}`
      }
    },
    grid: { left: '15%', right: '15%', top: '20%', bottom: '20%' },
    xAxis: {
      type: 'category',
      data: shortNames,
      axisLabel: { color: '#999', rotate: 30, fontSize: 11 }
    },
    yAxis: {
      type: 'category',
      data: shortNames,
      axisLabel: { color: '#999', fontSize: 11 }
    },
    visualMap: {
      min: -1,
      max: 1,
      calculable: true,
      orient: 'horizontal',
      left: 'center',
      bottom: '0%',
      inRange: {
        color: ['#313695', '#4575b4', '#74add1', '#fee090', '#f46d43', '#d73027']
      },
      textStyle: { color: '#999' }
    },
    series: [{
      name: '相关系数',
      type: 'heatmap',
      data: heatmapData,
      label: {
        show: true,
        formatter: (p: any) => p.data[2].toFixed(2),
        color: '#e0e0e0',
        fontSize: 10
      },
      emphasis: {
        itemStyle: {
          shadowBlur: 10,
          shadowColor: 'rgba(0, 0, 0, 0.5)'
        }
      }
    }]
  })
}

watch(() => props.data, () => {
  if (props.data && chartRef.value) {
    if (!echarts.getInstanceByDom(chartRef.value)) initChart()
    updateChart(buildOption(), true)
  }
}, { immediate: true })

onMounted(() => {
  if (chartRef.value && props.data && !echarts.getInstanceByDom(chartRef.value)) {
    initChart()
    updateChart(buildOption(), true)
  }
})
</script>
