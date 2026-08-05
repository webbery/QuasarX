<template>
  <div ref="chartEl" class="chart-container"></div>
</template>

<script setup lang="ts">
import { ref, onMounted, onBeforeUnmount, watch } from 'vue'
import * as echarts from 'echarts'
import type { Anomaly } from '../composables/useMLState'

const props = defineProps<{
  dates: string[]
  series: Record<string, (number | null)[]>     // key = symbol, value = 该 symbol 的值序列
  anomalies?: Anomaly[]                          // 用于在图上标注 jump_to_zero 等
  symbolField?: string                           // 显示在 tooltip 里的字段名（默认 "值"）
}>()

const ECHARTS_COLORS = ['#5b8ff9', '#26a65b', '#fac858', '#ee6677', '#aa87de']

let chart: echarts.ECharts | null = null
const chartEl = ref<HTMLDivElement | null>(null)

function render() {
  if (!chart) return
  const symbols = Object.keys(props.series)
  const fieldLabel = props.symbolField || '值'

  // 把 anomalies 按 symbol 索引，便于 markPoint 用
  const anomaliesBySymbol = new Map<string, Anomaly[]>()
  if (props.anomalies) {
    for (const a of props.anomalies) {
      if (!anomaliesBySymbol.has(a.symbol)) anomaliesBySymbol.set(a.symbol, [])
      anomaliesBySymbol.get(a.symbol)!.push(a)
    }
  }

  const seriesOpt = symbols.map((sym, idx) => {
    const values = props.series[sym]
    const symAnomalies = anomaliesBySymbol.get(sym) || []
    const color = ECHARTS_COLORS[idx % ECHARTS_COLORS.length]
    return {
      name: sym,
      type: 'line',
      data: values,
      showSymbol: false,
      connectNulls: false,  // NaN 处断开，更直观显示缺失
      lineStyle: { width: 1.5, color },
      itemStyle: { color },
      emphasis: { focus: 'series' },
      // markPoint 仅对 jump_to_zero 标注（其他异常在 ③ 文字报告里看）
      markPoint: symAnomalies.filter(a => a.type === 'jump_to_zero').length > 0 ? {
        symbol: 'pin',
        symbolSize: 28,
        itemStyle: { color: '#ef4444' },
        label: { color: '#fff', fontSize: 9, formatter: '↓0' },
        data: symAnomalies
          .filter(a => a.type === 'jump_to_zero')
          .map(a => {
            const xIdx = props.dates.indexOf(a.start_date)
            return xIdx >= 0 ? { name: '跳0', coord: [xIdx, props.series[sym][xIdx]] } : null
          })
          .filter((p): p is { name: string; coord: [number, number | null] } => p !== null),
      } : undefined,
    }
  })

  const opts: echarts.EChartsOption = {
    backgroundColor: 'transparent',
    tooltip: {
      trigger: 'axis',
      backgroundColor: 'rgba(15,25,41,0.95)',
      borderColor: '#2b3a55',
      textStyle: { color: '#e0e0e0', fontSize: 12 },
      formatter: ((params: any) => {
        const arr: any[] = Array.isArray(params) ? params : [params]
        if (arr.length === 0) return ''
        const date = arr[0].axisValueLabel
        let html = `<b>${date}</b>`
        for (const p of arr) {
          const v = p.value
          const txt = v === null || v === undefined ? 'NaN' :
            (typeof v === 'number' && Math.abs(v) >= 1000 ? v.toFixed(1) :
             typeof v === 'number' && Math.abs(v) >= 1 ? v.toFixed(3) :
             typeof v === 'number' ? v.toExponential(2) : String(v))
          html += `<br/><span style="color:${p.color}">●</span> ${p.seriesName}: ${txt}`
        }
        return html
      }) as any,
    },
    legend: {
      data: symbols,
      top: 4,
      textStyle: { color: '#94a3b8', fontSize: 11 },
      icon: 'roundRect',
    },
    grid: { left: 60, right: 30, top: 50, bottom: 60 },
    xAxis: {
      type: 'category',
      data: props.dates,
      axisLine: { lineStyle: { color: '#444' } },
      axisLabel: { color: '#94a3b8', fontSize: 10, hideOverlap: true },
    },
    yAxis: {
      type: 'value',
      scale: true,
      name: fieldLabel,
      nameTextStyle: { color: '#94a3b8', fontSize: 11 },
      axisLine: { lineStyle: { color: '#444' } },
      axisLabel: { color: '#94a3b8' },
      splitLine: { lineStyle: { color: 'rgba(255,255,255,0.05)' } },
    },
    dataZoom: [
      { type: 'inside', start: 0, end: 100 },
      { type: 'slider', start: 0, end: 100, height: 18, bottom: 8, borderColor: '#334155', fillerColor: 'rgba(91,143,249,0.2)', handleStyle: { color: '#5b8ff9' }, textStyle: { color: '#94a3b8', fontSize: 10 } },
    ],
    series: seriesOpt as any,
  }
  chart.setOption(opts, true)
}

onMounted(() => {
  if (chartEl.value) {
    chart = echarts.init(chartEl.value)
    render()
  }
  window.addEventListener('resize', handleResize)
})

onBeforeUnmount(() => {
  window.removeEventListener('resize', handleResize)
  chart?.dispose()
  chart = null
})

function handleResize() { chart?.resize() }

watch(
  () => [props.dates, props.series, props.anomalies],
  render,
  { deep: true },
)
</script>

<style scoped>
.chart-container { width: 100%; height: 360px; }
</style>
