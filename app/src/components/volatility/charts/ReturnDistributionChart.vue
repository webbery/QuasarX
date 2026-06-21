<template>
  <div class="return-distribution-chart" ref="chartRef" style="width: 100%; height: 100%"></div>
</template>

<script setup lang="ts">
import { watch, onMounted } from 'vue'
import * as echarts from 'echarts'
import { useECharts, createBaseChartOption } from '../../report/composables/useECharts'
import type { VolatilitySingleResult } from '../composables/useVolatilityState'

const props = defineProps<{
  data: VolatilitySingleResult | null
}>()

const { chartRef, initChart, updateChart } = useECharts(false) // 不自动初始化

function buildOption() {
  if (!props.data || !props.data.returns) return {}
  
  const returns = props.data.returns
  const n = returns.length
  
  // 计算直方图
  const bins = 30
  const min = Math.min(...returns)
  const max = Math.max(...returns)
  const binWidth = (max - min) / bins
  
  const histogram: { value: number; count: number }[] = []
  for (let i = 0; i < bins; i++) {
    const low = min + i * binWidth
    const high = low + binWidth
    const count = returns.filter(r => r >= low && (i === bins - 1 ? r <= high : r < high)).length
    histogram.push({ value: (low + high) / 2, count })
  }
  
  // 正态拟合
  const mean = returns.reduce((a, b) => a + b, 0) / n
  const variance = returns.reduce((a, b) => a + (b - mean) ** 2, 0) / (n - 1)
  const std = Math.sqrt(variance)
  
  const normalCurve = histogram.map(h => {
    const z = (h.value - mean) / std
    const density = Math.exp(-0.5 * z * z) / (std * Math.sqrt(2 * Math.PI))
    return density * n * binWidth
  })
  
  return createBaseChartOption({
    title: {
      text: '收益率分布',
      left: 'center',
      textStyle: { color: '#e0e0e0', fontSize: 14 }
    },
    tooltip: {
      trigger: 'axis',
      formatter: (params: any[]) => {
        const p = params[0]
        return `${p.name}<br/>频数: ${p.value}`
      }
    },
    grid: { left: '3%', right: '8%', bottom: '10%', top: '15%', containLabel: true },
    xAxis: {
      type: 'category',
      data: histogram.map(h => (h.value * 100).toFixed(2) + '%'),
      axisLabel: {
        color: '#999',
        rotate: 45,
        interval: Math.floor(bins / 10)
      }
    },
    yAxis: {
      type: 'value',
      name: '频数',
      axisLabel: { color: '#999' },
      nameTextStyle: { color: '#999' }
    },
    series: [
      {
        name: '频数',
        type: 'bar',
        data: histogram.map(h => h.count),
        itemStyle: { color: 'rgba(41, 98, 255, 0.6)' },
        barGap: '0%'
      },
      {
        name: '正态拟合',
        type: 'line',
        data: normalCurve,
        smooth: true,
        lineStyle: { color: '#ff9800', width: 2 },
        showSymbol: false
      }
    ],
    graphic: {
      type: 'group',
      left: 'center',
      bottom: 5,
      children: [
        {
          type: 'text',
          style: {
            text: `偏度: ${props.data.skewness.toFixed(3)}  峰度: ${props.data.kurtosis.toFixed(3)}  均值: ${(mean * 100).toFixed(3)}%  σ: ${(std * 100).toFixed(3)}%`,
            fill: '#a0aec0',
            fontSize: 11
          }
        }
      ]
    }
  })
}

watch(() => props.data, () => {
  if (props.data && chartRef.value) {
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

<style scoped>
.return-distribution-chart {
  min-height: 300px;
}
</style>
