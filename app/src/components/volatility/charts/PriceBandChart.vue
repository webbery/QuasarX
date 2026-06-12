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
  if (!props.data?.prices) return {}
  
  const prices = props.data.prices
  const upper2 = props.data.upper_2sigma
  const upper1 = props.data.upper_1sigma
  const lower1 = props.data.lower_1sigma
  const lower2 = props.data.lower_2sigma
  const mean = props.data.mean_price
  
  const len = prices.length
  
  return createBaseChartOption({
    title: {
      text: '价格与波动率包络带',
      left: 'center',
      textStyle: { color: '#e0e0e0', fontSize: 14 }
    },
    tooltip: { trigger: 'axis' },
    legend: { data: ['价格', '±1σ', '±2σ'], top: 25, textStyle: { color: '#999' } },
    grid: { left: '3%', right: '4%', bottom: '3%', top: '18%', containLabel: true },
    xAxis: {
      type: 'category',
      data: Array.from({ length: len }, (_, i) => i),
      axisLabel: { color: '#999', interval: Math.floor(len / 10) }
    },
    yAxis: {
      type: 'value',
      axisLabel: { color: '#999' }
    },
    series: [
      {
        name: '±2σ',
        type: 'line',
        data: upper2,
        lineStyle: { color: '#ef232a', width: 1, type: 'dashed' },
        showSymbol: false,
        smooth: true
      },
      {
        name: '±2σ',
        type: 'line',
        data: lower2,
        lineStyle: { color: '#ef232a', width: 1, type: 'dashed' },
        showSymbol: false,
        smooth: true,
        areaStyle: {
          color: new echarts.graphic.LinearGradient(0, 0, 0, 1, [
            { offset: 0, color: 'rgba(239, 35, 42, 0.05)' },
            { offset: 1, color: 'rgba(239, 35, 42, 0.05)' }
          ])
        }
      },
      {
        name: '±1σ',
        type: 'line',
        data: upper1,
        lineStyle: { color: '#ff9800', width: 1, type: 'dotted' },
        showSymbol: false,
        smooth: true
      },
      {
        name: '±1σ',
        type: 'line',
        data: lower1,
        lineStyle: { color: '#ff9800', width: 1, type: 'dotted' },
        showSymbol: false,
        smooth: true,
        areaStyle: {
          color: new echarts.graphic.LinearGradient(0, 0, 0, 1, [
            { offset: 0, color: 'rgba(255, 152, 0, 0.1)' },
            { offset: 1, color: 'rgba(255, 152, 0, 0.1)' }
          ])
        }
      },
      {
        name: '价格',
        type: 'line',
        data: prices,
        lineStyle: { color: '#2962ff', width: 2 },
        showSymbol: false,
        smooth: false
      }
    ]
  })
}

watch(() => props.data, () => {
  if (props.data) {
    if (chartRef.value && !echarts.getInstanceByDom(chartRef.value)) initChart()
    updateChart(buildOption(), true)
  }
}, { immediate: true })
</script>
