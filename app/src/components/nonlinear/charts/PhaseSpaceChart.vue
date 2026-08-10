<template>
  <div ref="chartRef" class="chart-container"></div>
</template>

<script setup lang="ts">
import { ref, watch, onMounted, onBeforeUnmount } from 'vue'
import * as echarts from 'echarts'
import 'echarts-gl'
import type { PhaseSpaceData } from '../composables/useNonlinearState'

const props = defineProps<{ data: PhaseSpaceData | null }>()
const chartRef = ref<HTMLDivElement>()
let chart: echarts.EChartsType | null = null

function render() {
  if (!chart || !props.data || !props.data.trajectory.length) return
  const { trajectory, trajectory_time, embed_dim } = props.data

  // 取前三维
  const has3D = embed_dim >= 3
  const tMin = trajectory_time[0]
  const tMax = trajectory_time[trajectory_time.length - 1]
  const tRange = tMax - tMin || 1

  if (has3D) {
    const lineData = trajectory.map((pt, i) => [pt[0], pt[1], pt[2], trajectory_time[i]])

    chart.setOption({
      title: {
        text: `相空间轨迹 (m=${embed_dim}, τ=${props.data.time_delay})`,
        textStyle: { color: '#e0e0e0', fontSize: 13 },
        left: 'center'
      },
      tooltip: {
        backgroundColor: 'rgba(26,34,54,0.95)',
        borderColor: '#2a3449',
        textStyle: { color: '#e0e0e0' },
        formatter: (params: any) => {
          const d = params.data
          return `x=${d[0].toFixed(4)}<br/>y=${d[1].toFixed(4)}<br/>z=${d[2].toFixed(4)}`
        }
      },
      xAxis3D: {
        name: 'x(t)',
        type: 'value',
        nameTextStyle: { color: '#999' },
        axisLabel: { color: '#999' },
        axisLine: { lineStyle: { color: 'rgba(74,85,104,0.4)' } }
      },
      yAxis3D: {
        name: `x(t+τ)`,
        type: 'value',
        nameTextStyle: { color: '#999' },
        axisLabel: { color: '#999' },
        axisLine: { lineStyle: { color: 'rgba(74,85,104,0.4)' } }
      },
      zAxis3D: {
        name: `x(t+2τ)`,
        type: 'value',
        nameTextStyle: { color: '#999' },
        axisLabel: { color: '#999' },
        axisLine: { lineStyle: { color: 'rgba(74,85,104,0.4)' } }
      },
      grid3D: {
        viewControl: {
          projection: 'perspective',
          autoRotate: true,
          autoRotateSpeed: 5,
          distance: 200
        },
        light: {
          main: { intensity: 1.2, shadow: false },
          ambient: { intensity: 0.3 }
        },
        environment: 'transparent'
      },
      series: [{
        type: 'line3D',
        data: lineData,
        lineStyle: { width: 1.5 },
        visualMap: {
          show: false,
          dimension: 3,
          min: tMin,
          max: tMax,
          inRange: {
            color: ['#2962ff', '#00c853', '#ff9800', '#ff6d00', '#ff1744']
          }
        }
      }]
    }, true)
  } else {
    // 2D fallback
    const scatterData = trajectory.map((pt, i) => [pt[0], pt[1], trajectory_time[i]])

    chart.setOption({
      title: {
        text: `相空间轨迹 (m=${embed_dim}, τ=${props.data.time_delay})`,
        textStyle: { color: '#e0e0e0', fontSize: 13 }
      },
      tooltip: {
        backgroundColor: 'rgba(26,34,54,0.95)',
        textStyle: { color: '#e0e0e0' }
      },
      grid: { left: 60, right: 30, top: 50, bottom: 40 },
      xAxis: {
        type: 'value', name: 'x(t)',
        axisLine: { lineStyle: { color: '#444' } },
        axisLabel: { color: '#999' }
      },
      yAxis: {
        type: 'value', name: 'x(t+τ)',
        axisLine: { lineStyle: { color: '#444' } },
        axisLabel: { color: '#999' }
      },
      visualMap: {
        show: false, dimension: 2,
        min: tMin, max: tMax,
        inRange: { color: ['#2962ff', '#00c853', '#ff9800', '#ff1744'] }
      },
      series: [{
        type: 'scatter',
        data: scatterData,
        symbolSize: 3,
      }]
    }, true)
  }
}

onMounted(() => {
  if (!chartRef.value) return
  chart = echarts.init(chartRef.value)
  render()
  const ro = new ResizeObserver(() => chart?.resize())
  ro.observe(chartRef.value)
  onBeforeUnmount(() => {
    ro.disconnect()
    chart?.dispose()
    chart = null
  })
})

watch(() => props.data, render, { deep: true })
</script>

<style scoped>
.chart-container { width: 100%; height: 100%; min-height: 400px; }
</style>
