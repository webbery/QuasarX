<template>
  <div class="emd-chart" ref="chartRef" style="width: 100%; height: 100%"></div>
</template>

<script setup lang="ts">
import { watch, computed } from 'vue'
import * as echarts from 'echarts'
import { useECharts, createBaseChartOption } from '../../report/composables/useECharts'
import type { SignalAnalysisResult } from '../composables/useSignalState'

const props = defineProps<{
  data: SignalAnalysisResult | null
}>()

const { chartRef, initChart, updateChart } = useECharts()

const IMF_COLORS = [
  '#2962ff', '#ff9800', '#00c853', '#ff6d00', '#a0aec0',
  '#e040fb', '#ffab40', '#69f0ae', '#ff5252', '#7c4dff',
  '#40c4ff', '#ffd740', '#b2ff59', '#ff6e40', '#ea80fc',
  '#18ffff', '#eeff41', '#ff80ab', '#b388ff', '#84ffff'
]

function buildOption() {
  if (!props.data) return {}

  const { dates, original, imf_components, residual, imf_info } = props.data
  const numIMFs = imf_components.length

  // 构建子图：原始信号 + 各IMF + 残差
  const totalRows = 1 + numIMFs + 1 // original + IMFs + residual
  const rowHeight = 100 / totalRows

  const series: any[] = []
  const yAxes: any[] = []

  // 原始信号
  series.push({
    name: '原始信号',
    type: 'line',
    data: original.map((v: number) => Number(v.toFixed(4))),
    xAxisIndex: 0,
    yAxisIndex: 0,
    lineStyle: { color: '#e0e0e0', width: 1.5 },
    showSymbol: false,
    smooth: true
  })

  // 各 IMF
  imf_components.forEach((imf: number[], idx: number) => {
    series.push({
      name: `IMF${idx + 1}`,
      type: 'line',
      data: imf.map((v: number) => Number(v.toFixed(6))),
      xAxisIndex: 0,
      yAxisIndex: idx + 1,
      lineStyle: { color: IMF_COLORS[idx % IMF_COLORS.length], width: 1 },
      showSymbol: false,
      smooth: true
    })
  })

  // 残差
  series.push({
    name: '残差',
    type: 'line',
    data: residual.map((v: number) => Number(v.toFixed(6))),
    xAxisIndex: 0,
    yAxisIndex: totalRows - 1,
    lineStyle: { color: '#a0aec0', width: 1, type: 'dashed' },
    showSymbol: false,
    smooth: true
  })

  // Y 轴配置（每个子图独立 Y 轴）
  yAxes.push({
    show: true,
    position: 'left',
    splitNumber: 3,
    axisLabel: { color: '#999', fontSize: 10, formatter: '{value}' },
    splitLine: { show: true, lineStyle: { color: 'rgba(74, 85, 104, 0.2)' } }
  })
  for (let i = 1; i < totalRows - 1; i++) {
    const info = imf_info[i - 1]
    yAxes.push({
      show: true,
      position: 'left',
      splitNumber: 2,
      axisLabel: {
        color: '#999',
        fontSize: 10,
        formatter: (val: number) => val.toFixed(3)
      },
      splitLine: { show: true, lineStyle: { color: 'rgba(74, 85, 104, 0.15)' } },
      name: `IMF${i} T=${info?.mean_period.toFixed(0) || '-'} E=${info?.energy_pct.toFixed(1) || '0'}%`,
      nameTextStyle: { color: IMF_COLORS[(i - 1) % IMF_COLORS.length], fontSize: 10 },
      nameLocation: 'end'
    })
  }
  // 残差 Y 轴
  yAxes.push({
    show: true,
    position: 'left',
    splitNumber: 2,
    axisLabel: { color: '#999', fontSize: 10 },
    splitLine: { show: true, lineStyle: { color: 'rgba(74, 85, 104, 0.15)' } },
    name: '残差',
    nameTextStyle: { color: '#a0aec0', fontSize: 10 },
    nameLocation: 'end'
  })

  // 共享 X 轴（只在底部显示）
  const xAxisData = dates && dates.length > 0
    ? dates.map((d: string) => d.length > 10 ? d.substring(5, 10) : d)
    : original.map((_: number, i: number) => String(i))

  return createBaseChartOption({
    title: {
      text: `EMD 分解结果 (${props.data.method.toUpperCase()})  重建误差: ${(props.data.reconstruction_error * 1e10).toFixed(2)}e-10`,
      left: 'center',
      textStyle: { color: '#e0e0e0', fontSize: 13 }
    },
    tooltip: {
      trigger: 'axis',
      backgroundColor: 'rgba(26, 34, 54, 0.95)',
      borderColor: '#2a3449',
      textStyle: { color: '#e0e0e0', fontSize: 11 },
      axisPointer: { type: 'line', link: { xAxisIndex: 'all' } }
    },
    grid: {
      left: '4%',
      right: '4%',
      top: '8%',
      bottom: '5%'
    },
    xAxis: [{
      type: 'category',
      data: xAxisData,
      axisLabel: {
        color: '#999',
        fontSize: 10,
        interval: Math.floor(xAxisData.length / 8)
      },
      axisLine: { lineStyle: { color: 'rgba(74, 85, 104, 0.3)' } }
    }],
    yAxis: yAxes,
    series
  })
}

watch(() => props.data, () => {
  if (props.data) {
    if (chartRef.value && !echarts.getInstanceByDom(chartRef.value)) {
      initChart()
    }
    updateChart(buildOption(), true)
  }
}, { immediate: true })
</script>

<style scoped>
.emd-chart {
  min-height: 600px;
}
</style>
