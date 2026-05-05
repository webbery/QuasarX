<!-- app/src/components/report/charts/PerformanceChart.vue -->
<!-- 策略绩效总览图（合并：策略性能 + 基准对比） -->

<template>
  <div class="chart-card full-width">
    <div class="chart-title">
      <div class="title-icon">📈</div>
      <span>Strategy Performance</span>
      <span v-if="isEstimated" class="estimated-badge">⚠️ 估算值</span>
      <div class="chart-controls">
        <label>基准</label>
        <select v-model="localBenchmark" @change="handleBenchmarkChange" class="benchmark-select">
          <option value="">-- 无 --</option>
          <option v-for="idx in benchmarkIndices" :key="idx.code" :value="idx.code">
            {{ idx.name }} ({{ idx.code }})
          </option>
        </select>
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
  dailyReturns: number[]
  benchmarkData: any[]
  benchmarkName: string
  selectedBenchmark: string
  isEstimated?: boolean
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
 * 从基准数据生成 x 轴日期标签
 * 优先使用基准数据的时间戳，确保所有数据点都能显示
 */
const xAxisDates = computed(() => {
  if (props.benchmarkData.length > 0) {
    return props.benchmarkData.map((d: any) => {
      const ms = d.time
      const date = new Date(ms > 9999999999 ? ms : ms * 1000)
      const Y = date.getFullYear()
      const M = String(date.getMonth() + 1).padStart(2, '0')
      const D = String(date.getDate()).padStart(2, '0')
      return `${Y}-${M}-${D}`
    })
  }
  return props.dates
})

/**
 * 计算基准日收益率
 */
const benchmarkDailyReturns = computed(() => {
  if (props.benchmarkData.length === 0) return []

  const returns: number[] = [0] // 第一天收益率为 0
  for (let i = 1; i < props.benchmarkData.length; i++) {
    const prevClose = props.benchmarkData[i - 1].close
    const currClose = props.benchmarkData[i].close
    const dailyReturn = ((currClose - prevClose) / prevClose) * 100
    returns.push(Number(dailyReturn.toFixed(4)))
  }
  return returns
})

/**
 * 计算基准累计收益曲线（从日收益率连乘）
 */
const benchmarkCumulativeReturns = computed(() => {
  const returns = benchmarkDailyReturns.value
  if (returns.length === 0) return []

  const result: number[] = []
  let cumulative = 1

  for (const dailyReturn of returns) {
    cumulative *= (1 + dailyReturn / 100)
    result.push(Number(((cumulative - 1) * 100).toFixed(2)))
  }

  return result
})

/**
 * 将日收益率对齐到基准日期轴
 * 当日收益率点数与基准不一致时，使用前向填充或截断
 */
const alignedDailyReturns = computed(() => {
  const benchLen = benchmarkCumulativeReturns.value.length
  const dailyReturns = props.dailyReturns ?? []
  const stratLen = dailyReturns.length

  if (benchLen === 0) return dailyReturns
  if (benchLen === stratLen) return dailyReturns

  // 日收益率点数少于基准：前向填充到基准长度
  if (stratLen < benchLen && stratLen > 0) {
    const result: number[] = []
    const ratio = stratLen / benchLen
    let lastValue = dailyReturns[0]

    for (let i = 0; i < benchLen; i++) {
      // 计算当前基准点对应的策略索引
      const srcIdx = Math.floor(i * ratio)

      if (srcIdx < stratLen) {
        // 有对应的日收益率数据，更新 lastValue
        lastValue = dailyReturns[srcIdx]
      }
      // 否则继续使用 lastValue（前向填充）

      result.push(lastValue)
    }
    return result
  }

  // 日收益率点数多于基准：截断
  if (stratLen > benchLen) {
    return dailyReturns.slice(0, benchLen)
  }

  return dailyReturns
})

/**
 * 计算累计收益率（从日收益率连乘）
 */
const cumulativeReturns = computed(() => {
  const returns = alignedDailyReturns.value
  if (returns.length === 0) return []

  const result: number[] = []
  let cumulative = 1 // 初始净值 = 1

  for (const dailyReturn of returns) {
    cumulative *= (1 + dailyReturn / 100) // 日收益率是百分比，需要除以 100
    result.push(Number(((cumulative - 1) * 100).toFixed(2))) // 转换为百分比形式
  }

  return result
})

/**
 * 构建 ECharts 配置
 */
function buildChartOption() {
  const hasBenchmark = props.benchmarkData.length > 0
  const dates = xAxisDates.value
  const dailyReturns = alignedDailyReturns.value
  const cumReturns = cumulativeReturns.value

  const series: any[] = [
    {
      name: '累计收益',
      type: 'line',
      data: cumReturns,
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
          const color = item.seriesName === '累计收益' ? '#2962ff' : '#ff9800'
          result += `<div>${item.marker} ${item.seriesName}: <span style="color: ${color}; font-weight: bold;">${value}%</span></div>`
        })
        return result
      }
    },
    legend: {
      data: hasBenchmark ? ['累计收益', props.benchmarkName] : ['累计收益'],
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
      data: dates,
      axisLine: { lineStyle: { color: '#6E7079' } },
      axisLabel: {
        color: '#a0aec0',
        formatter: (value: string) => {
          if (!value) return value
          // 已经是 YYYY-MM-DD 格式，直接返回
          if (/^\d{4}-\d{2}-\d{2}$/.test(value)) return value
          const date = new Date(value)
          if (isNaN(date.getTime())) return value
          const Y = date.getFullYear()
          const M = String(date.getMonth() + 1).padStart(2, '0')
          const D = String(date.getDate()).padStart(2, '0')
          return `${Y}-${M}-${D}`
        }
      }
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
  [() => props.dates, () => props.dailyReturns, () => props.benchmarkData, () => props.benchmarkName, xAxisDates, cumulativeReturns],
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

.estimated-badge {
  font-size: 12px;
  color: #ff6d00;
  padding: 2px 8px;
  background: rgba(255, 109, 0, 0.15);
  border-radius: 4px;
  font-weight: 500;
  animation: pulse 2s ease-in-out infinite;
}

@keyframes pulse {
  0%, 100% { opacity: 1; }
  50% { opacity: 0.6; }
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
