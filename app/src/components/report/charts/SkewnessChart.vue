<!-- app/src/components/report/charts/SkewnessChart.vue -->
<!-- 偏度与峰度分析图 -->

<template>
  <div class="chart-card">
    <div class="chart-title">
      <div class="title-icon">⚖️</div>
      <span>Skewness & Kurtosis</span>
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
import { computed, watch, onMounted } from 'vue'
import * as echarts from 'echarts'
import { useECharts, createBaseChartOption } from '../composables/useECharts'

interface Props {
  /** 偏度值 */
  skewness?: number
  /** 峰度值 */
  kurtosis?: number
  /** 收益数据（用于计算分布） */
  returnsData?: number[]
}

const props = withDefaults(defineProps<Props>(), {
  skewness: 0.35,
  kurtosis: 3.12,
  returnsData: () => []
})

const { chartRef, initChart, updateChart } = useECharts(true)

// === 计算属性 ===

const skewnessValue = computed(() => props.skewness)
const kurtosisValue = computed(() => props.kurtosis)

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

// === 图表配置 ===

/**
 * 生成正态分布曲线数据
 */
function generateNormalDistribution(mean = 0, std = 1, points = 100) {
  const data: number[][] = []
  const range = 4 * std // ±4 标准差
  
  for (let i = 0; i <= points; i++) {
    const x = mean - range + (2 * range * i) / points
    const y = (1 / (std * Math.sqrt(2 * Math.PI))) * 
              Math.exp(-0.5 * Math.pow((x - mean) / std, 2))
    data.push([Number(x.toFixed(3)), Number(y.toFixed(6))])
  }
  
  return data
}

/**
 * 构建 ECharts 配置
 */
function buildChartOption() {
  // 如果有收益数据，使用数据计算分布
  // 否则使用理论正态分布
  let distributionData: number[][] = []
  
  if (props.returnsData.length > 0) {
    // TODO: 根据实际收益数据计算偏度和峰度
    // 当前使用理论值
    distributionData = generateNormalDistribution(0, 1)
  } else {
    distributionData = generateNormalDistribution(0, 1)
  }

  return createBaseChartOption({
    tooltip: {
      trigger: 'axis',
      backgroundColor: 'rgba(26, 34, 54, 0.9)',
      borderColor: '#2a3449',
      textStyle: { color: '#e0e0e0' },
      formatter: function(params: any) {
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
        data: distributionData,
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

// === 生命周期 ===

watch(
  [() => props.skewness, () => props.kurtosis, () => props.returnsData],
  () => {
    updateChart(buildChartOption(), true)
  },
  { deep: true }
)

onMounted(() => {
  console.info('[SkewnessChart] 组件已挂载')
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
