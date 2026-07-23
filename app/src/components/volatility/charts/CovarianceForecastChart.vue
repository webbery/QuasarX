<template>
  <div class="forecast-card">
    <div class="forecast-header">
      <h3 class="forecast-title">
        {{ data?.horizon || 0 }}步协方差膨胀矩阵
        <TipHint content="用历史 AR 模型外推 h 期后的协方差矩阵，反映预测期内资产共同运动的演变。预测期相关性 vs 真实相关性的差可用于识别相关性抬升（危机前兆）现象" />
      </h3>
    </div>
    <div ref="chartRef" class="chart-container"></div>
  </div>
</template>

<script setup lang="ts">
import { watch, onMounted } from 'vue'
import * as echarts from 'echarts'
import { useECharts, createBaseChartOption } from '../../report/composables/useECharts'
import type { MultiForecast } from '../composables/useVolatilityState'
import TipHint from '../../TipHint.vue'

const props = defineProps<{
  data: MultiForecast | null
}>()

const { chartRef, initChart, updateChart } = useECharts(false)

function buildOption() {
  if (!props.data?.forecast_corr) return {}

  const { symbols, forecast_corr, forecast_cov } = props.data
  const n = symbols.length

  // 构建热力图数据
  const heatmapData: [number, number, number][] = []
  for (let i = 0; i < n; i++) {
    for (let j = 0; j < n; j++) {
      heatmapData.push([j, i, forecast_corr[i][j]])
    }
  }

  const shortNames = symbols.map(s => {
    const parts = s.split('.')
    return parts.length > 1 ? parts[0] : s
  })

  return createBaseChartOption({
    tooltip: {
      formatter: (p: any) => {
        const i = p.data[1], j = p.data[0]
        return `${symbols[i]} ↔ ${symbols[j]}<br/>相关系数: ${p.data[2].toFixed(3)}<br/>协方差: ${forecast_cov[i][j].toExponential(4)}`
      }
    },
    grid: { left: '15%', right: '15%', top: '10%', bottom: '15%' },
    xAxis: {
      type: 'category',
      data: shortNames,
      axisLabel: { color: '#999', rotate: 30, fontSize: 11 }
    },
    yAxis: {
      type: 'category',
      data: shortNames,
      axisLabel: { color: '#999', fontSize: 11 }
    },
    visualMap: {
      min: -1,
      max: 1,
      calculable: true,
      orient: 'vertical',
      right: 10,
      top: 'center',
      inRange: {
        color: ['#313695', '#4575b4', '#74add1', '#fee090', '#f46d43', '#d73027']
      },
      textStyle: { color: '#999' }
    },
    series: [{
      name: '相关系数',
      type: 'heatmap',
      data: heatmapData,
      label: {
        show: true,
        formatter: (p: any) => p.data[2].toFixed(2),
        color: '#e0e0e0',
        fontSize: 10
      },
      emphasis: {
        itemStyle: {
          shadowBlur: 10,
          shadowColor: 'rgba(0, 0, 0, 0.5)'
        }
      }
    }]
  })
}

watch(() => props.data, () => {
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

<style scoped>
.forecast-card {
  display: flex;
  flex-direction: column;
  width: 100%;
  height: 100%;
}

.forecast-header {
  flex-shrink: 0;
  padding-bottom: 8px;
}

.forecast-title {
  margin: 0;
  font-size: 14px;
  color: #e0e0e0;
  font-weight: 600;
  display: flex;
  align-items: center;
  gap: 4px;
}

.chart-container {
  flex: 1;
  min-height: 0;
}
</style>
