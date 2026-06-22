<template>
  <div ref="chartRef" style="width: 100%; height: 100%"></div>
</template>

<script setup lang="ts">
import { watch, onMounted } from 'vue'
import * as echarts from 'echarts'
import { useECharts, createBaseChartOption } from '../../report/composables/useECharts'
import type { VolatilitySingleResult, ForecastResult } from '../composables/useVolatilityState'

const props = defineProps<{
  data: VolatilitySingleResult | null
  forecast: ForecastResult | null
  dates?: string[]
}>()

const { chartRef, initChart, updateChart } = useECharts(false)

function buildOption() {
  if (!props.data?.prices) return {}

  const prices = props.data.prices
  const upper2 = props.data.upper_2sigma
  const upper1 = props.data.upper_1sigma
  const lower1 = props.data.lower_1sigma
  const lower2 = props.data.lower_2sigma
  const mean = props.data.mean_price
  const len = prices.length

  const dates = props.dates || []
  const displayDates = dates.length > 0 ? dates.slice(-len) : Array.from({ length: len }, (_, i) => i)

  const series: any[] = []

  // === 历史数据 series ===
  series.push({
    name: '±2σ',
    type: 'line',
    data: upper2,
    lineStyle: { color: '#ef232a', width: 1, type: 'dashed' },
    showSymbol: false,
    smooth: true
  })
  series.push({
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
  })
  series.push({
    name: '±1σ',
    type: 'line',
    data: upper1,
    lineStyle: { color: '#ff9800', width: 1, type: 'dotted' },
    showSymbol: false,
    smooth: true
  })
  series.push({
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
  })
  series.push({
    name: '价格',
    type: 'line',
    data: prices,
    lineStyle: { color: '#2962ff', width: 2 },
    showSymbol: false,
    smooth: false
  })

  // === 预测段 ===
  const forecast = props.forecast
  if (forecast?.has_autocorrelation && forecast.forecast_values.length > 0) {
    const fcSteps = forecast.forecast_values.length
    const lastPrice = prices[len - 1]

    // 预测价格序列（基于最后价格推导）
    const fcPrices = forecast.forecast_values.map(v => lastPrice * (1 + v))
    const fcUpper1 = forecast.forecast_upper_1sigma.map(v => lastPrice + (v - lastPrice))
    const fcLower1 = forecast.forecast_lower_1sigma.map(v => lastPrice - (lastPrice - v))
    const fcUpper2 = forecast.forecast_upper_2sigma.map(v => lastPrice + (v - lastPrice) * 1.5)
    const fcLower2 = forecast.forecast_lower_2sigma.map(v => lastPrice - (lastPrice - v) * 1.5)

    // x 轴扩展
    const fcLabels = Array.from({ length: fcSteps }, (_, i) => `预测+${i + 1}`)
    const allDates = [...displayDates, ...fcLabels]

    // 拼接数据（历史 + 预测）
    const padArray = (arr: number[], fill: number, count: number) => {
      const result = [...arr]
      for (let i = 0; i < count; i++) result.push(fill)
      return result
    }

    // 渐变颜色生成
    const gradStops = (r: number, g: number, b: number, alphaStart: number) => {
      const stops: { offset: number; color: string }[] = []
      for (let i = 0; i <= fcSteps; i++) {
        const t = i / fcSteps
        const alpha = alphaStart * Math.pow(1 - t, 1.5)
        stops.push({ offset: t, color: `rgba(${r},${g},${b},${alpha.toFixed(4)})` })
      }
      return stops
    }

    // 预测 ±2σ
    series.push({
      name: '预测±2σ',
      type: 'line',
      data: [...upper2, ...fcUpper2],
      xAxisIndex: 0,
      lineStyle: { color: '#ef232a', width: 1, type: 'dashed' },
      showSymbol: false
    })
    series.push({
      name: '预测±2σ',
      type: 'line',
      data: [...lower2, ...fcLower2],
      lineStyle: { color: '#ef232a', width: 1, type: 'dashed' },
      showSymbol: false,
      areaStyle: {
        color: new echarts.graphic.LinearGradient(0, 0, 1, 0, gradStops(239, 35, 42, 0.05))
      }
    })

    // 预测 ±1σ
    series.push({
      name: '预测±1σ',
      type: 'line',
      data: [...upper1, ...fcUpper1],
      lineStyle: { color: '#ff9800', width: 1, type: 'dotted' },
      showSymbol: false
    })
    series.push({
      name: '预测±1σ',
      type: 'line',
      data: [...lower1, ...fcLower1],
      lineStyle: { color: '#ff9800', width: 1, type: 'dotted' },
      showSymbol: false,
      areaStyle: {
        color: new echarts.graphic.LinearGradient(0, 0, 1, 0, gradStops(255, 152, 0, 0.12))
      }
    })

    // 预测价格线
    series.push({
      name: '预测价格',
      type: 'line',
      data: [...prices, ...fcPrices],
      lineStyle: { color: '#2962ff', width: 2, type: 'dashed' },
      showSymbol: false
    })

    // 历史/预测分界垂直线
    series.push({
      type: 'markLine',
      symbol: 'none',
      data: [{ xAxis: len - 1 }],
      lineStyle: { color: '#555', type: 'dotted', width: 1 },
      label: { show: true, formatter: '预测起点', color: '#999', fontSize: 10, position: 'insideStartTop' }
    })

    return createBaseChartOption({
      title: {
        text: '价格与波动率包络带',
        left: 'center',
        textStyle: { color: '#e0e0e0', fontSize: 14 }
      },
      tooltip: {
        trigger: 'axis',
        formatter: (params: any[]) => {
          if (!params || params.length === 0) return ''
          let result = `${params[0].axisValue}<br/>`
          for (const param of params) {
            const value = typeof param.value === 'number' ? param.value.toFixed(3) : param.value
            result += `${param.marker}${param.seriesName}: ${value}<br/>`
          }
          return result
        }
      },
      legend: { data: ['价格', '±1σ', '±2σ', '预测价格', '预测±1σ', '预测±2σ'], top: 25, textStyle: { color: '#999' } },
      grid: { left: '3%', right: '4%', bottom: '3%', top: '18%', containLabel: true },
      xAxis: {
        type: 'category',
        data: allDates,
        axisLabel: {
          color: '#999',
          interval: Math.floor(allDates.length / 10),
          rotate: 30,
          formatter: (val: string) => {
            if (typeof val === 'string' && val.includes('-') && !val.startsWith('预测')) {
              const parts = val.split('-')
              if (parts.length === 3) return `${parts[1]}-${parts[2]}`
            }
            return val
          }
        }
      },
      yAxis: {
        type: 'value',
        axisLabel: { color: '#999' }
      },
      series
    })
  }

  // === 无预测：原始渲染 ===
  return createBaseChartOption({
    title: {
      text: '价格与波动率包络带',
      left: 'center',
      textStyle: { color: '#e0e0e0', fontSize: 14 }
    },
    tooltip: {
      trigger: 'axis',
      formatter: (params: any[]) => {
        if (!params || params.length === 0) return ''
        let result = `${params[0].axisValue}<br/>`
        for (const param of params) {
          const value = typeof param.value === 'number' ? param.value.toFixed(3) : param.value
          result += `${param.marker}${param.seriesName}: ${value}<br/>`
        }
        return result
      }
    },
    legend: { data: ['价格', '±1σ', '±2σ'], top: 25, textStyle: { color: '#999' } },
    grid: { left: '3%', right: '4%', bottom: '3%', top: '18%', containLabel: true },
    xAxis: {
      type: 'category',
      data: displayDates,
      axisLabel: {
        color: '#999',
        interval: Math.floor(len / 10),
        rotate: 30,
        formatter: (val: string) => {
          if (typeof val === 'string' && val.includes('-')) {
            const parts = val.split('-')
            if (parts.length === 3) return `${parts[1]}-${parts[2]}`
          }
          return val
        }
      }
    },
    yAxis: {
      type: 'value',
      axisLabel: { color: '#999' }
    },
    series
  })
}

watch(() => props.data, () => {
  if (props.data && chartRef.value) {
    if (!echarts.getInstanceByDom(chartRef.value)) initChart()
    updateChart(buildOption(), true)
  }
}, { immediate: true })

watch(() => props.forecast, () => {
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
