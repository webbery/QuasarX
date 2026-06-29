<template>
  <div class="scree-plot">
    <h4>碎石图 (Scree Plot)</h4>
    <div ref="chartRef" class="chart-container"></div>
    <div v-if="selectedK > 0" class="k-selector">
      当前选择: 保留前 <strong>{{ selectedK }}</strong> 个主成分
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, watch, onMounted, onBeforeUnmount } from 'vue'
import * as echarts from 'echarts'
import type { PCACrossSectionResult } from '../composables/usePCAState'

const props = defineProps<{
  data: PCACrossSectionResult | null
  selectedK: number
}>()

const emit = defineEmits<{
  (e: 'update:k', k: number): void
}>()

const chartRef = ref<HTMLElement>()
let chartInstance: echarts.EChartsType | null = null

function buildOption() {
  if (!props.data) return {}

  const { eigenvalues, variance_ratio, cumulative_variance } = props.data
  const n = eigenvalues.length
  const indices = Array.from({ length: n }, (_, i) => i + 1)

  // 自动检测拐点
  let elbowIndex = 1
  if (n >= 3) {
    let maxDiff = 0
    for (let i = 1; i < n - 1; i++) {
      const diff = (eigenvalues[i - 1] - eigenvalues[i]) - (eigenvalues[i] - eigenvalues[i + 1])
      if (diff > maxDiff) {
        maxDiff = diff
        elbowIndex = i
      }
    }
  }

  return {
    tooltip: {
      trigger: 'axis' as const,
      formatter: (params: any[]) => {
        const p = params[0]
        const pc = p.dataIndex + 1
        return `PC${pc}<br/>特征值: ${eigenvalues[p.dataIndex].toFixed(3)}<br/>方差解释: ${(variance_ratio[p.dataIndex] * 100).toFixed(1)}%<br/>累计: ${(cumulative_variance[p.dataIndex] * 100).toFixed(1)}%`
      }
    },
    grid: { left: 50, right: 50, top: 40, bottom: 60 },
    xAxis: {
      type: 'category' as const,
      data: indices.map(i => `PC${i}`),
      name: '主成分',
      nameLocation: 'middle' as const,
      nameGap: 35,
      axisLabel: { color: '#999' },
      axisLine: { lineStyle: { color: '#555' } },
      splitLine: { show: false }
    },
    yAxis: [
      {
        type: 'value' as const,
        name: '特征值',
        nameTextStyle: { color: '#2962ff' },
        axisLabel: { color: '#999' },
        axisLine: { lineStyle: { color: '#2962ff' } },
        splitLine: { lineStyle: { color: 'rgba(74, 85, 104, 0.3)' } }
      },
      {
        type: 'value' as const,
        name: '累计方差 (%)',
        min: 0,
        max: 100,
        axisLabel: { color: '#999', formatter: '{value}%' },
        axisLine: { lineStyle: { color: '#ff9800' } },
        splitLine: { show: false }
      }
    ],
    series: [
      {
        name: '特征值',
        type: 'bar' as const,
        data: eigenvalues,
        itemStyle: { color: '#2962ff' },
        barMaxWidth: 40,
        yAxisIndex: 0,
        markLine: {
          data: [{ yAxis: 1, label: { formatter: 'Kaiser λ=1' } }],
          lineStyle: { color: '#ef232a', type: 'dashed' },
          label: { color: '#ef232a', position: 'insideStartTop' }
        },
        markPoint: {
          data: [{ coord: [elbowIndex, eigenvalues[elbowIndex]], label: { formatter: '拐点' } }],
          itemStyle: { color: '#00c853' },
          label: { color: '#00c853', fontWeight: 'bold' }
        }
      },
      {
        name: '累计方差',
        type: 'line' as const,
        data: cumulative_variance.map(v => v * 100),
        smooth: true,
        itemStyle: { color: '#ff9800' },
        lineStyle: { width: 3 },
        yAxisIndex: 1,
        symbol: 'circle',
        symbolSize: 6
      }
    ]
  }
}

function initChart() {
  if (!chartRef.value) return
  chartInstance = echarts.init(chartRef.value)
  chartInstance.setOption(buildOption())

  chartInstance.on('click', (params: any) => {
    if (params.componentType === 'series' && params.seriesName === '特征值') {
      emit('update:k', params.dataIndex + 1)
    }
  })
}

function updateChart() {
  chartInstance?.setOption(buildOption(), true)
}

watch(() => props.data, (newData) => {
  if (newData) {
    if (!chartInstance && chartRef.value) initChart()
    else if (chartInstance) updateChart()
  }
}, { immediate: true })

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
.scree-plot {
  background: rgba(26, 34, 54, 0.9);
  border-radius: 8px;
  padding: 16px;
}
.chart-container {
  width: 100%;
  height: 350px;
}
.k-selector {
  text-align: center;
  margin-top: 8px;
  color: #00c853;
  font-size: 14px;
}
</style>
