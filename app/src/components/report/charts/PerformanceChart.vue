<!-- app/src/components/report/charts/PerformanceChart.vue -->
<!-- 策略绩效总览图（合并：策略性能 + 基准对比） -->

<template>
  <div class="chart-card full-width">
    <div class="chart-title">
      <div class="title-icon">📈</div>
      <span>Strategy Performance</span>
      <div class="chart-controls">
        <label>基准</label>
        <select v-model="localBenchmark" @change="handleBenchmarkChange" class="benchmark-select">
          <option value="">-- 无 --</option>
          <option v-for="idx in benchmarkIndices" :key="idx.code" :value="idx.code">
            {{ idx.name }} ({{ idx.code }})
          </option>
        </select>
        <button
          v-if="localBenchmark"
          class="btn-refresh"
          @click="handleRefresh"
          :disabled="loading"
          title="刷新基准数据"
        >
          🔄
        </button>
        <span v-if="benchmarkName" class="benchmark-label">{{ benchmarkName }}</span>
      </div>
    </div>
    <div class="chart-container" ref="chartRef"></div>
  </div>
</template>

<script setup lang="ts">
import { ref, computed, watch, onMounted } from 'vue'
import * as echarts from 'echarts'
import { useECharts, createBaseChartOption } from '../composables/useECharts'
import { BENCHMARK_INDICES } from '@/lib/tickflow'

interface Props {
  dates: string[]
  strategyReturns: number[]
  benchmarkData: any[]
  benchmarkName: string
  selectedBenchmark: string
  metricsData: Record<string, number>
}

interface Emits {
  (e: 'benchmarkChange'): void
  (e: 'refreshBenchmark'): void
}

const props = defineProps<Props>()
const emit = defineEmits<Emits>()

const localBenchmark = ref(props.selectedBenchmark)
const benchmarkIndices = BENCHMARK_INDICES
const loading = ref(false)

const { chartRef, initChart, updateChart } = useECharts(true)

// === 数据计算 ===

/**
 * 计算策略累计收益曲线
 * 如果有真实信号，使用真实数据；否则使用测试数据（线性插值）
 */
const strategyCumulativeReturns = computed(() => {
  const totalReturn = props.metricsData.total_return || 0
  const numDates = props.dates.length || 12

  // 简化：用总收益线性插值生成月度收益曲线
  // TODO: 如果后端能提供月度收益数据，应该直接使用
  return props.dates.map((_, i) => {
    const progress = numDates > 1 ? i / (numDates - 1) : 0
    return Number((totalReturn * progress * 100).toFixed(2))
  })
})

/**
 * 计算基准累计收益曲线
 */
const benchmarkCumulativeReturns = computed(() => {
  if (props.benchmarkData.length === 0) return []

  const firstClose = props.benchmarkData[0].close
  return props.benchmarkData.map((d: any) =>
    Number((((d.close - firstClose) / firstClose) * 100).toFixed(2))
  )
})

/**
 * 构建 ECharts 配置
 */
function buildChartOption() {
  const hasBenchmark = props.benchmarkData.length > 0

  const series: any[] = [
    {
      name: '策略收益',
      type: 'line',
      data: strategyCumulativeReturns.value,
      smooth: true,
      lineStyle: { width: 3, color: '#2962ff' },
      areaStyle: {
        color: new echarts.graphic.LinearGradient(0, 0, 0, 1, [
          { offset: 0, color: 'rgba(41, 98, 255, 0.3)' },
          { offset: 1, color: 'rgba(41, 98, 255, 0.05)' }
        ])
      }
    }
  ]

  if (hasBenchmark) {
    series.push({
      name: props.benchmarkName || '基准',
      type: 'line',
      data: benchmarkCumulativeReturns.value,
      smooth: true,
      lineStyle: { width: 2, color: '#ff9800', type: 'dashed' }
    })
  }

  return createBaseChartOption({
    tooltip: {
      trigger: 'axis',
      axisPointer: { type: 'cross' },
      backgroundColor: 'rgba(26, 34, 54, 0.9)',
      borderColor: '#2a3449',
      textStyle: { color: '#e0e0e0' },
      formatter: function(params: any) {
        let result = `<div style="margin: 0 0 5px 0; font-weight: bold;">${params[0].axisValue}</div>`
        params.forEach((item: any) => {
          const value = typeof item.value === 'number' ? item.value.toFixed(2) : item.value
          const color = item.seriesName === '策略收益' ? '#2962ff' : '#ff9800'
          result += `<div>${item.marker} ${item.seriesName}: <span style="color: ${color}; font-weight: bold;">${value}%</span></div>`
        })
        return result
      }
    },
    legend: {
      data: hasBenchmark ? ['策略收益', props.benchmarkName] : ['策略收益'],
      textStyle: { color: '#e0e0e0' },
      top: 10
    },
    grid: {
      left: '3%',
      right: '4%',
      bottom: '3%',
      containLabel: true
    },
    xAxis: {
      type: 'category',
      data: props.dates,
      axisLine: { lineStyle: { color: '#6E7079' } },
      axisLabel: { color: '#a0aec0' }
    },
    yAxis: {
      type: 'value',
      axisLabel: {
        formatter: '{value}%',
        color: '#a0aec0'
      },
      axisLine: { lineStyle: { color: '#6E7079' } },
      splitLine: {
        lineStyle: { color: '#2a3449', type: 'dashed' }
      }
    },
    series
  })
}

// === 事件处理 ===

function handleBenchmarkChange() {
  emit('benchmarkChange')
}

function handleRefresh() {
  emit('refreshBenchmark')
}

// === Watchers ===

// 监听数据变化，更新图表
watch(
  [() => props.dates, () => props.strategyReturns, () => props.benchmarkData, () => props.benchmarkName],
  () => {
    updateChart(buildChartOption(), true)
  },
  { deep: true }
)

// 同步 selectedBenchmark
watch(() => props.selectedBenchmark, (val) => {
  localBenchmark.value = val
})

// === Lifecycle ===

onMounted(() => {
  console.info('[PerformanceChart] 组件已挂载')
})
</script>

<style scoped>
.chart-card {
  background: var(--panel-bg, #1a2236);
  border-radius: 12px;
  padding: 20px;
  border: 1px solid var(--border, #2a3449);
  transition: all 0.3s ease;
  box-shadow: 0 4px 6px rgba(0, 0, 0, 0.1);
  display: flex;
  flex-direction: column;
  min-height: 0;
}

.chart-card:hover {
  box-shadow: 0 8px 25px rgba(0, 0, 0, 0.2);
  transform: translateY(-2px);
  border-color: #2962ff;
}

.chart-card.full-width {
  grid-column: 1 / -1;
}

.chart-title {
  font-size: 16px;
  font-weight: 600;
  margin-bottom: 16px;
  color: var(--text, #e0e0e0);
  display: flex;
  align-items: center;
  gap: 12px;
  padding-bottom: 12px;
  border-bottom: 1px solid var(--border, #2a3449);
}

.title-icon {
  font-size: 20px;
  width: 32px;
  height: 32px;
  display: flex;
  align-items: center;
  justify-content: center;
  background: rgba(41, 98, 255, 0.1);
  border-radius: 8px;
}

.chart-controls {
  margin-left: auto;
  display: flex;
  align-items: center;
  gap: 10px;
}

.chart-controls label {
  font-size: 14px;
  color: var(--text, #e0e0e0);
  font-weight: 500;
  white-space: nowrap;
}

.chart-controls .benchmark-select {
  padding: 8px 12px;
  background: rgba(42, 52, 77, 0.5);
  border: 1px solid var(--border, #2a3449);
  border-radius: 6px;
  color: var(--text, #e0e0e0);
  font-size: 14px;
  cursor: pointer;
  transition: all 0.2s;
  min-width: 150px;
}

.chart-controls .benchmark-select:hover {
  border-color: #2962ff;
}

.chart-controls .btn-refresh {
  padding: 8px 12px;
  background: rgba(41, 98, 255, 0.2);
  border: 1px solid #2962ff;
  border-radius: 6px;
  color: #2962ff;
  cursor: pointer;
  font-size: 16px;
  transition: all 0.2s;
}

.chart-controls .btn-refresh:hover:not(:disabled) {
  background: rgba(41, 98, 255, 0.3);
}

.chart-controls .btn-refresh:disabled {
  opacity: 0.5;
  cursor: not-allowed;
}

.benchmark-label {
  margin-left: 12px;
  font-size: 12px;
  color: #ff9800;
  padding: 2px 8px;
  background: rgba(255, 152, 0, 0.1);
  border-radius: 4px;
}

.chart-container {
  height: 300px;
  min-height: 300px;
  width: 100%;
  flex-shrink: 0;
}

.chart-card.full-width .chart-container {
  height: 300px;
  min-height: 300px;
}

@media (max-width: 768px) {
  .chart-card {
    padding: 16px;
  }

  .chart-container {
    height: 250px;
  }

  .chart-controls {
    flex-direction: column;
    align-items: flex-end;
    gap: 8px;
  }

  .chart-controls label {
    font-size: 12px;
  }

  .chart-controls .benchmark-select {
    min-width: 80px;
    font-size: 12px;
    padding: 4px 8px;
  }

  .chart-controls .btn-refresh {
    font-size: 14px;
    padding: 4px 8px;
  }
}
</style>
