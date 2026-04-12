<!-- app/src/components/report/charts/SkewnessChart.vue -->
<!-- 偏度与峰度分析图 -->

<template>
  <div class="chart-card">
    <div class="chart-title">
      <div class="title-icon">⚖️</div>
      <span>Skewness & Kurtosis</span>
      <span v-if="isCalculating" class="loading-indicator">计算中...</span>
      <span v-else-if="calculationTime" class="calc-time">{{ calculationTime }}ms</span>
    </div>
    <div class="chart-container" ref="chartRef"></div>

    <!-- 统计高亮卡片 -->
    <div class="stats-highlight">
      <div class="stat-item">
        <div class="stat-value" :class="skewnessClass">{{ skewnessValue.toFixed(2) }}</div>
        <div class="stat-label">Skewness</div>
      </div>
      <div class="stat-item">
        <div class="stat-value" :class="kurtosisClass">{{ kurtosisValue.toFixed(2) }}</div>
        <div class="stat-label">Kurtosis</div>
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, computed, watch, onMounted, onUnmounted, toRaw } from 'vue'
import * as echarts from 'echarts'
import { useECharts, createBaseChartOption } from '../composables/useECharts'
import { generateNormalDistribution } from '@/lib/statistics'

interface Props {
  /** 偏度值（后端传入，备用） */
  skewness?: number
  /** 峰度值（后端传入，备用） */
  kurtosis?: number
  /** 收益数据（用于计算分布） */
  returnsData?: number[]
  /** 价格数据 [date, close][] - 用于前端计算 */
  prices?: Array<[string, number]>
}

const props = withDefaults(defineProps<Props>(), {
  skewness: 0.35,
  kurtosis: 3.12,
  returnsData: () => [],
  prices: () => []
})

const { chartRef, initChart, updateChart } = useECharts(true)

// === Worker 状态 ===
let worker: Worker | null = null
const isCalculating = ref(false)
const calculationTime = ref<string>('')

// 前端计算结果
const calculatedStats = ref<{
  skewness: number
  kurtosis: number
  mean: number
  std: number
  histogram: { bins: number[]; counts: number[] }
} | null>(null)

// === 计算属性 ===

// 优先使用前端计算结果，否则使用后端传入值
const skewnessValue = computed(() => calculatedStats.value?.skewness ?? props.skewness)
const kurtosisValue = computed(() => calculatedStats.value?.kurtosis ?? props.kurtosis)

/**
 * 偏值样式类名
 * 偏度 > 0: 正偏（右偏）- 绿色
 * 偏度 < 0: 负偏（左偏）- 橙色
 * 偏度 ≈ 0: 对称 - 灰色
 */
const skewnessClass = computed(() => {
  const val = Math.abs(skewnessValue.value)
  if (val < 0.5) return 'neutral'
  return skewnessValue.value > 0 ? 'positive' : 'negative'
})

/**
 * 峰度样式类名
 * 峰度 > 3: 尖峰厚尾 - 橙色
 * 峰度 < 3: 低峰薄尾 - 绿色
 * 峰度 ≈ 3: 正态分布 - 灰色
 */
const kurtosisClass = computed(() => {
  const diff = Math.abs(kurtosisValue.value - 3)
  if (diff < 0.5) return 'neutral'
  return kurtosisValue.value > 3 ? 'negative' : 'positive'
})

// === Worker 管理 ===

/**
 * 初始化 Web Worker
 */
function initWorker() {
  worker = new Worker(
    new URL('@/workers/statistics.worker.ts', import.meta.url),
    { type: 'module' }
  )

  worker.onmessage = function (e: MessageEvent) {
    const result = e.data
    isCalculating.value = false

    if (result.success) {
      calculatedStats.value = {
        skewness: result.skewness,
        kurtosis: result.kurtosis,
        mean: result.mean,
        std: result.std,
        histogram: result.histogram
      }
      calculationTime.value = String(result.duration)
      console.info(
        `[SkewnessChart] Worker 计算完成: ${result.dataPoints} 个数据点, 耗时 ${result.duration}ms`
      )
      updateChart(buildChartOption(), true)
    } else {
      console.error('[SkewnessChart] Worker 计算失败:', result.error)
    }
  }

  worker.onerror = function (error) {
    console.error('[SkewnessChart] Worker 错误:', error)
    isCalculating.value = false
  }
}

/**
 * 触发计算
 */
function triggerCalculation() {
  if (!props.prices || props.prices.length < 2 || !worker) {
    return
  }

  isCalculating.value = true
  worker.postMessage({ prices: toRaw(props.prices) })
}

// === 图表配置 ===

/**
 * 构建 ECharts 配置
 */
function buildChartOption() {
  const stats = calculatedStats.value

  // 如果没有前端计算结果，使用后端传入值生成理论曲线
  if (!stats) {
    const normalData = generateNormalDistribution(0, 1)
    return createBaseChartOption({
      tooltip: {
        trigger: 'axis',
        backgroundColor: 'rgba(26, 34, 54, 0.9)',
        borderColor: '#2a3449',
        textStyle: { color: '#e0e0e0' },
        formatter: function (params: any) {
          if (!params.length) return ''
          const item = params[0]
          return `
            <div style="font-weight: bold; margin-bottom: 5px;">${item.axisValue}</div>
            <div>${item.marker} 密度: <span style="color: #2962ff; font-weight: bold;">${item.value[1].toFixed(4)}</span></div>
          `
        }
      },
      grid: {
        left: '3%',
        right: '4%',
        bottom: '3%',
        containLabel: true
      },
      xAxis: {
        type: 'value',
        name: '收益率',
        nameTextStyle: { color: '#a0aec0' },
        axisLine: { lineStyle: { color: '#6E7079' } },
        axisLabel: {
          color: '#a0aec0',
          formatter: '{value}%'
        },
        splitLine: {
          lineStyle: { color: '#2a3449', type: 'dashed' }
        }
      },
      yAxis: {
        type: 'value',
        name: '密度',
        nameTextStyle: { color: '#a0aec0' },
        axisLine: { lineStyle: { color: '#6E7079' } },
        axisLabel: { color: '#a0aec0' },
        splitLine: {
          lineStyle: { color: '#2a3449', type: 'dashed' }
        }
      },
      series: [
        {
          name: '正态分布',
          type: 'line',
          data: normalData,
          smooth: true,
          lineStyle: { width: 3, color: '#2962ff' },
          areaStyle: {
            color: new echarts.graphic.LinearGradient(0, 0, 0, 1, [
              { offset: 0, color: 'rgba(41, 98, 255, 0.4)' },
              { offset: 1, color: 'rgba(41, 98, 255, 0.05)' }
            ])
          },
          symbol: 'none'
        }
      ]
    })
  }

  // 使用实际计算的 mean 和 std 生成正态分布曲线
  const normalData = generateNormalDistribution(stats.mean, stats.std)

  // 构建系列数据：直方图 + 正态分布拟合曲线
  const histogramData = stats.histogram.bins.map((bin, i) => [bin, stats.histogram.counts[i]])

  return createBaseChartOption({
    tooltip: {
      trigger: 'axis',
      backgroundColor: 'rgba(26, 34, 54, 0.9)',
      borderColor: '#2a3449',
      textStyle: { color: '#e0e0e0' },
      formatter: function (params: any) {
        if (!params.length) return ''
        const item = params[0]
        return `
          <div style="font-weight: bold; margin-bottom: 5px;">${item.axisValue}</div>
          <div>${item.marker} ${item.seriesName}: <span style="color: #2962ff; font-weight: bold;">${item.value[1].toFixed(6)}</span></div>
        `
      }
    },
    legend: {
      data: ['实际分布', '正态分布拟合'],
      textStyle: { color: '#a0aec0' },
      top: 10
    },
    grid: {
      left: '3%',
      right: '4%',
      bottom: '3%',
      containLabel: true
    },
    xAxis: {
      type: 'value',
      name: '收益率',
      nameTextStyle: { color: '#a0aec0' },
      axisLine: { lineStyle: { color: '#6E7079' } },
      axisLabel: {
        color: '#a0aec0',
        formatter: '{value}%'
      },
      splitLine: {
        lineStyle: { color: '#2a3449', type: 'dashed' }
      }
    },
    yAxis: {
      type: 'value',
      name: '密度',
      nameTextStyle: { color: '#a0aec0' },
      axisLine: { lineStyle: { color: '#6E7079' } },
      axisLabel: { color: '#a0aec0' },
      splitLine: {
        lineStyle: { color: '#2a3449', type: 'dashed' }
      }
    },
    series: [
      {
        name: '实际分布',
        type: 'bar',
        data: histogramData,
        barWidth: '60%',
        itemStyle: {
          color: 'rgba(41, 98, 255, 0.3)',
          borderColor: '#2962ff',
          borderWidth: 1
        }
      },
      {
        name: '正态分布拟合',
        type: 'line',
        data: normalData,
        smooth: true,
        lineStyle: { width: 3, color: '#ff6d00' },
        symbol: 'none'
      }
    ]
  })
}

// === 生命周期 ===

watch(
  () => props.prices,
  () => {
    triggerCalculation()
  }
)

// 监听后端传入值变化（备用）
watch(
  [() => props.skewness, () => props.kurtosis, () => props.returnsData],
  () => {
    // 如果没有前端计算结果，使用后端值更新图表
    if (!calculatedStats.value) {
      updateChart(buildChartOption(), true)
    }
  },
  { deep: true }
)

onMounted(() => {
  initWorker()
  console.info('[SkewnessChart] 组件已挂载')
})

onUnmounted(() => {
  worker?.terminate()
  worker = null
  console.info('[SkewnessChart] Worker 已终止')
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

.loading-indicator {
  margin-left: auto;
  font-size: 12px;
  color: #2962ff;
  animation: pulse 1.5s ease-in-out infinite;
}

.calc-time {
  margin-left: auto;
  font-size: 11px;
  color: var(--text-secondary, #a0aec0);
}

@keyframes pulse {
  0%, 100% { opacity: 1; }
  50% { opacity: 0.5; }
}

.chart-container {
  height: 250px;
  min-height: 250px;
  width: 100%;
  flex-shrink: 0;
}

.stats-highlight {
  display: flex;
  gap: 20px;
  margin-top: 20px;
  padding-top: 20px;
  border-top: 1px solid var(--border, #2a3449);
}

.stat-item {
  text-align: center;
  flex: 1;
  padding: 12px;
  background: rgba(42, 52, 77, 0.3);
  border-radius: 8px;
  border: 1px solid var(--border, #2a3449);
}

.stat-value {
  font-size: 28px;
  font-weight: bold;
  margin-bottom: 8px;
}

.stat-label {
  font-size: 13px;
  color: var(--text-secondary, #a0aec0);
  text-transform: uppercase;
  letter-spacing: 0.5px;
}

.positive {
  color: #00c853;
}

.neutral {
  color: #ff6d00;
}

.negative {
  color: #ff1744;
}

@media (max-width: 768px) {
  .chart-card {
    padding: 16px;
  }

  .chart-container {
    height: 200px;
  }

  .stats-highlight {
    flex-direction: column;
    gap: 12px;
  }

  .stat-value {
    font-size: 24px;
  }
}
</style>
