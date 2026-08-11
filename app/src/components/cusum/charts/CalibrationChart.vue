<template>
  <div class="calibration-charts">
    <div class="chart-row">
      <div class="chart-panel">
        <div class="chart-title">ARL₀ — H 曲线 (λ={{ lambda.toFixed(2) }})</div>
        <div ref="arlChartRef" class="chart"></div>
      </div>
      <div class="chart-panel">
        <div class="chart-title">σ 估计稳定性 (Bootstrap CV)</div>
        <div ref="cvChartRef" class="chart"></div>
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, onMounted, onUnmounted, watch, nextTick } from 'vue'
import * as echarts from 'echarts'

interface CalibrationData {
  arl_curve_H: number[]
  arl_curve_arl: number[]
  bootstrap_sizes: number[]
  bootstrap_cv: number[]
  H: number
  actual_arl0: number
  min_obs: number
}

interface Props {
  data: CalibrationData
  targetArl0: number
  lambda?: number
}

const props = withDefaults(defineProps<Props>(), {
  lambda: 0.5,
})

const arlChartRef = ref<HTMLElement>()
const cvChartRef = ref<HTMLElement>()
let arlChart: echarts.ECharts | null = null
let cvChart: echarts.ECharts | null = null

function renderArlChart() {
  if (!arlChart || !props.data.arl_curve_H?.length) return

  const H = props.data.H
  const actualArl = props.data.actual_arl0
  const targetArl = props.targetArl0

  arlChart.setOption({
    backgroundColor: 'transparent',
    tooltip: {
      trigger: 'axis',
      backgroundColor: 'rgba(26,34,54,0.95)',
      borderColor: '#2a3449',
      textStyle: { color: '#e0e0e0', fontSize: 12 },
      formatter: (params: any) => {
        const p = params[0]
        return `H = ${p.data[0].toFixed(2)}<br/>ARL₀ = ${Math.round(p.data[1])} 天`
      }
    },
    grid: { left: 60, right: 30, top: 30, bottom: 40 },
    xAxis: {
      type: 'value',
      name: 'H (阈值倍数)',
      nameTextStyle: { color: '#888', fontSize: 11 },
      axisLine: { lineStyle: { color: '#2a3449' } },
      axisLabel: { color: '#888' },
      splitLine: { lineStyle: { color: 'rgba(42,52,77,0.5)' } },
    },
    yAxis: {
      type: 'value',
      name: 'ARL₀ (天)',
      nameTextStyle: { color: '#888', fontSize: 11 },
      axisLine: { lineStyle: { color: '#2a3449' } },
      axisLabel: { color: '#888' },
      splitLine: { lineStyle: { color: 'rgba(42,52,77,0.5)' } },
    },
    series: [
      {
        type: 'line',
        data: props.data.arl_curve_H.map((h, i) => [h, props.data.arl_curve_arl[i]]),
        smooth: true,
        lineStyle: { color: '#2962ff', width: 2 },
        itemStyle: { color: '#2962ff' },
        showSymbol: true,
        symbolSize: 4,
        areaStyle: {
          color: new echarts.graphic.LinearGradient(0, 0, 0, 1, [
            { offset: 0, color: 'rgba(41,98,255,0.15)' },
            { offset: 1, color: 'rgba(41,98,255,0)' },
          ])
        },
        markPoint: {
          data: [
            {
              coord: [H, actualArl],
              symbol: 'circle',
              symbolSize: 10,
              itemStyle: { color: '#ff6d00' },
              label: {
                show: true,
                formatter: `H=${H.toFixed(2)}\nARL₀=${Math.round(actualArl)}`,
                position: 'top',
                color: '#ff6d00',
                fontSize: 11,
                fontWeight: 'bold',
              }
            }
          ]
        },
        markLine: {
          silent: true,
          lineStyle: { color: '#ff1744', type: 'dashed', width: 1 },
          data: [
            {
              yAxis: targetArl,
              label: { formatter: `目标 ARL₀=${targetArl}`, color: '#ff1744', fontSize: 10 }
            }
          ]
        }
      }
    ]
  })
}

function renderCvChart() {
  if (!cvChart || !props.data.bootstrap_sizes?.length) return

  const minObs = props.data.min_obs

  cvChart.setOption({
    backgroundColor: 'transparent',
    tooltip: {
      trigger: 'axis',
      backgroundColor: 'rgba(26,34,54,0.95)',
      borderColor: '#2a3449',
      textStyle: { color: '#e0e0e0', fontSize: 12 },
      formatter: (params: any) => {
        const p = params[0]
        return `N = ${Math.round(p.data[0])}<br/>CV(σ̂) = ${(p.data[1] * 100).toFixed(1)}%`
      }
    },
    grid: { left: 60, right: 30, top: 30, bottom: 40 },
    xAxis: {
      type: 'value',
      name: '样本量 N',
      nameTextStyle: { color: '#888', fontSize: 11 },
      axisLine: { lineStyle: { color: '#2a3449' } },
      axisLabel: { color: '#888' },
      splitLine: { lineStyle: { color: 'rgba(42,52,77,0.5)' } },
    },
    yAxis: {
      type: 'value',
      name: 'CV(σ̂)',
      nameTextStyle: { color: '#888', fontSize: 11 },
      axisLabel: {
        color: '#888',
        formatter: (v: number) => `${(v * 100).toFixed(0)}%`,
      },
      axisLine: { lineStyle: { color: '#2a3449' } },
      splitLine: { lineStyle: { color: 'rgba(42,52,77,0.5)' } },
    },
    series: [
      {
        type: 'line',
        data: props.data.bootstrap_sizes.map((s, i) => [s, props.data.bootstrap_cv[i]]),
        smooth: true,
        lineStyle: { color: '#00c853', width: 2 },
        itemStyle: { color: '#00c853' },
        showSymbol: true,
        symbolSize: 4,
        areaStyle: {
          color: new echarts.graphic.LinearGradient(0, 0, 0, 1, [
            { offset: 0, color: 'rgba(0,200,83,0.15)' },
            { offset: 1, color: 'rgba(0,200,83,0)' },
          ])
        },
        markPoint: {
          data: [
            {
              coord: [minObs, props.data.bootstrap_cv[props.data.bootstrap_sizes.indexOf(minObs)] ?? 0],
              symbol: 'circle',
              symbolSize: 10,
              itemStyle: { color: '#ff6d00' },
              label: {
                show: true,
                formatter: `min_obs=${minObs}`,
                position: 'top',
                color: '#ff6d00',
                fontSize: 11,
                fontWeight: 'bold',
              }
            }
          ]
        },
        markLine: {
          silent: true,
          lineStyle: { color: '#ff1744', type: 'dashed', width: 1 },
          data: [
            {
              yAxis: 0.15,
              label: { formatter: 'CV=15%', color: '#ff1744', fontSize: 10 }
            }
          ]
        }
      }
    ]
  })
}

function initCharts() {
  if (arlChartRef.value && !arlChart) {
    arlChart = echarts.init(arlChartRef.value)
  }
  if (cvChartRef.value && !cvChart) {
    cvChart = echarts.init(cvChartRef.value)
  }
}

function renderAll() {
  initCharts()
  renderArlChart()
  renderCvChart()
}

function handleResize() {
  arlChart?.resize()
  cvChart?.resize()
}

onMounted(() => {
  nextTick(renderAll)
  window.addEventListener('resize', handleResize)
})

onUnmounted(() => {
  window.removeEventListener('resize', handleResize)
  arlChart?.dispose()
  cvChart?.dispose()
})

watch(() => props.data, renderAll, { deep: true })
</script>

<style scoped>
.calibration-charts {
  width: 100%;
}

.chart-row {
  display: flex;
  gap: 16px;
}

.chart-panel {
  flex: 1;
  min-width: 0;
}

.chart-title {
  font-size: 12px;
  color: #999;
  padding: 8px 12px 0;
  font-weight: 500;
}

.chart {
  width: 100%;
  height: 280px;
}
</style>
