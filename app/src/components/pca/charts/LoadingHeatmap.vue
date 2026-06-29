<template>
  <div class="loading-heatmap">
    <h4>主成分载荷图 (Loading Plot)</h4>
    <div ref="chartRef" class="chart-container"></div>
  </div>
</template>

<script setup lang="ts">
import { ref, watch, onMounted, onBeforeUnmount } from 'vue'
import * as echarts from 'echarts'

const props = defineProps<{
  symbols: string[]
  loadings: number[][]
  nComponents: number
}>()

const chartRef = ref<HTMLElement>()
let chartInstance: echarts.EChartsType | null = null

function buildOption() {
  if (!props.symbols.length || !props.loadings.length) return {}

  const { symbols, loadings, nComponents } = props
  const pcs = Array.from({ length: nComponents }, (_, i) => `PC${i + 1}`)

  // 构建热力图数据
  const data: [number, number, number][] = []
  for (let i = 0; i < symbols.length; i++) {
    for (let j = 0; j < nComponents; j++) {
      data.push([j, i, loadings[i][j]])
    }
  }

  return {
    tooltip: {
      position: 'top' as const,
      formatter: (params: any) => {
        const [pc, sym, value] = params.data
        return `${symbols[sym]}<br/>${pcs[pc]}<br/>载荷: ${value.toFixed(3)}${Math.abs(value) > 0.4 ? ' ✓' : ''}`
      }
    },
    grid: { left: 120, right: 80, top: 20, bottom: 60 },
    xAxis: {
      type: 'category' as const,
      data: pcs,
      name: '主成分',
      nameLocation: 'middle' as const,
      nameGap: 40,
      axisLabel: { color: '#e0e0e0', fontSize: 12 },
      axisLine: { lineStyle: { color: '#555' } },
      splitArea: { show: true, areaStyle: { color: ['rgba(26, 34, 54, 0.9)', 'rgba(30, 40, 60, 0.9)'] } }
    },
    yAxis: {
      type: 'category' as const,
      data: symbols,
      axisLabel: { color: '#e0e0e0', fontSize: 11 },
      axisLine: { lineStyle: { color: '#555' } },
      splitArea: { show: true }
    },
    visualMap: {
      min: -1,
      max: 1,
      calculable: true,
      orient: 'horizontal' as const,
      left: 'center',
      bottom: 0,
      inRange: {
        color: ['#ef232a', '#1a2236', '#2962ff']  // 红(负) → 蓝(正)
      },
      text: ['+', '-'],
      textStyle: { color: '#999' }
    },
    series: [{
      type: 'heatmap' as const,
      data,
      label: {
        show: true,
        formatter: (p: any) => p.data[2].toFixed(2),
        color: '#fff',
        fontSize: 10
      },
      emphasis: {
        itemStyle: {
          shadowBlur: 10,
          shadowColor: 'rgba(0, 0, 0, 0.5)'
        }
      }
    }]
  }
}

function initChart() {
  if (!chartRef.value) return
  chartInstance = echarts.init(chartRef.value)
  chartInstance.setOption(buildOption())
}

function updateChart() {
  chartInstance?.setOption(buildOption(), true)
}

watch(() => props.loadings, () => {
  if (props.loadings.length > 0) {
    if (!chartInstance && chartRef.value) initChart()
    else if (chartInstance) updateChart()
  }
}, { immediate: true, deep: true })

onMounted(() => {
  window.addEventListener('resize', handleResize)
})

onBeforeUnmount(() => {
  window.removeEventListener('resize', handleResize)
  chartInstance?.dispose()
})

function handleResize() {
  chartInstance?.resize()
}
</script>

<style scoped>
.loading-heatmap {
  background: rgba(26, 34, 54, 0.9);
  border-radius: 8px;
  padding: 16px;
}
.chart-container {
  width: 100%;
  height: 400px;
}
</style>
