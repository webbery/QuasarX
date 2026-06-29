<template>
  <div class="correlation-comparison">
    <h4>相关系数矩阵对比 (原始 vs PCA重构)</h4>
    <div class="heatmap-row">
      <div class="heatmap-item">
        <h5>原始相关矩阵</h5>
        <div ref="originalRef" class="chart-container"></div>
      </div>
      <div class="heatmap-item">
        <h5>重构相关矩阵 (前{{ nComponents }}个PC)</h5>
        <div ref="reconRef" class="chart-container"></div>
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, watch, onMounted, onBeforeUnmount } from 'vue'
import * as echarts from 'echarts'

const props = defineProps<{
  symbols: string[]
  original: number[][]
  reconstructed: number[][]
  nComponents: number
}>()

const originalRef = ref<HTMLElement>()
const reconRef = ref<HTMLElement>()
let originalChart: echarts.EChartsType | null = null
let reconChart: echarts.EChartsType | null = null

function buildHeatmapOption(data: number[][], symbols: string[], title: string) {
  const n = symbols.length
  const heatmapData: [number, number, number][] = []
  for (let i = 0; i < n; i++) {
    for (let j = 0; j < n; j++) {
      heatmapData.push([j, i, data[i][j]])
    }
  }

  return {
    tooltip: {
      position: 'top' as const,
      formatter: (params: any) => {
        const [x, y, v] = params.data
        return `${symbols[y]} ↔ ${symbols[x]}<br/>ρ = ${v.toFixed(3)}`
      }
    },
    grid: { left: 100, right: 60, top: 10, bottom: 60 },
    xAxis: {
      type: 'category' as const,
      data: symbols,
      axisLabel: { color: '#e0e0e0', fontSize: 10, rotate: 45 },
      axisLine: { lineStyle: { color: '#555' } }
    },
    yAxis: {
      type: 'category' as const,
      data: symbols,
      axisLabel: { color: '#e0e0e0', fontSize: 10 },
      axisLine: { lineStyle: { color: '#555' } }
    },
    visualMap: {
      min: -1,
      max: 1,
      calculable: false,
      orient: 'vertical' as const,
      right: 0,
      top: 'center',
      inRange: {
        color: ['#ef232a', '#1a2236', '#2962ff']
      },
      textStyle: { color: '#999' },
      show: false
    },
    series: [{
      type: 'heatmap' as const,
      data: heatmapData,
      label: {
        show: n <= 10,
        formatter: (p: any) => p.data[2].toFixed(2),
        color: '#fff',
        fontSize: 9
      },
      emphasis: {
        itemStyle: { shadowBlur: 10, shadowColor: 'rgba(0,0,0,0.5)' }
      }
    }]
  }
}

function initCharts() {
  if (!props.symbols.length) return
  if (!originalRef.value || !reconRef.value) return

  originalChart = echarts.init(originalRef.value)
  reconChart = echarts.init(reconRef.value)

  originalChart.setOption(buildHeatmapOption(props.original, props.symbols, '原始'))
  reconChart.setOption(buildHeatmapOption(props.reconstructed, props.symbols, '重构'))
}

function updateCharts() {
  if (originalChart && reconChart && props.symbols.length) {
    originalChart.setOption(buildHeatmapOption(props.original, props.symbols, '原始'), true)
    reconChart.setOption(buildHeatmapOption(props.reconstructed, props.symbols, '重构'), true)
  }
}

watch(() => props.original, () => {
  if (props.original.length > 0) {
    if (!originalChart && originalRef.value) initCharts()
    else updateCharts()
  }
}, { immediate: true, deep: true })

onMounted(() => {
  window.addEventListener('resize', handleResize)
})

onBeforeUnmount(() => {
  window.removeEventListener('resize', handleResize)
  originalChart?.dispose()
  reconChart?.dispose()
})

function handleResize() {
  originalChart?.resize()
  reconChart?.resize()
}
</script>

<style scoped>
.correlation-comparison {
  background: rgba(26, 34, 54, 0.9);
  border-radius: 8px;
  padding: 16px;
}
.heatmap-row {
  display: flex;
  gap: 16px;
}
.heatmap-item {
  flex: 1;
  min-width: 0;
}
.heatmap-item h5 {
  margin: 0 0 8px 0;
  color: #e0e0e0;
  font-size: 13px;
  text-align: center;
}
.chart-container {
  width: 100%;
  height: 350px;
}
</style>
