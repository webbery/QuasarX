<template>
  <div class="spectrum-card">
    <h3 class="card-title">
      谱指标 m⁺/m⁻ 时间序列
      <TipHint content="基于 Marchenko-Pastur 随机矩阵理论。m⁻ 计算低于理论下界 λ₋ 的特征值数量，反映市场同步性/分散化失效。m⁻ 突增预示风险聚集（相关性结构趋近低秩）。参考 Grassia et al. (2026)。" />
    </h3>

    <div v-if="!hasData" class="empty-hint">
      需要至少 2 个标的且数据长度 > 滚动窗口 ({{ data?.window_size || 60 }})
    </div>

    <template v-else>
      <!-- 统计面板 -->
      <div class="stats-grid">
        <div class="stat-item">
          <span class="stat-label">当前 m⁻</span>
          <span class="stat-value" :class="mMinusClass">{{ currentMMinus }}</span>
        </div>
        <div class="stat-item">
          <span class="stat-label">当前 m⁺</span>
          <span class="stat-value">{{ currentMPlus }}</span>
        </div>
        <div class="stat-item">
          <span class="stat-label">
            m⁻/n 比率
            <TipHint :content="thresholdHint" />
          </span>
          <span class="stat-value" :class="ratioClass">{{ (currentMMinus / nEffective).toFixed(2) }}</span>
        </div>
        <div class="stat-item">
          <span class="stat-label">
            预警阈值
            <TipHint content="m⁻/n > 0.5 表示超过一半的特征值低于 MP 下界，分散化严重失效。论文实证：SPsector (n=10) 用 m⁻≥5 作为危机信号。" />
          </span>
          <span class="stat-value stat-dim">{{ alertThreshold.toFixed(2) }}</span>
        </div>
        <div class="stat-item">
          <span class="stat-label">有效标的 k / 原始 N</span>
          <span class="stat-value stat-dim">{{ nEffective }} / {{ data?.original_n }}</span>
        </div>
        <div class="stat-item">
          <span class="stat-label">窗口 / λ₊ / λ₋</span>
          <span class="stat-value stat-dim">{{ data?.window_size }} / {{ data?.lambda_plus?.toFixed(3) }} / {{ data?.lambda_minus?.toFixed(3) }}</span>
        </div>
      </div>

      <!-- 图表 -->
      <div ref="chartRef" class="chart-container"></div>
    </template>
  </div>
</template>

<script setup lang="ts">
import { ref, computed, watch, onMounted, onUnmounted, nextTick } from 'vue'
import * as echarts from 'echarts'
import TipHint from '../../TipHint.vue'

interface SpectrumData {
  dates: string[]
  m_plus: number[]
  m_minus: number[]
  n_effective: number[]
  lambda_plus: number
  lambda_minus: number
  original_n: number
  window_size: number
}

const props = defineProps<{
  data: SpectrumData | null
}>()

const chartRef = ref<HTMLElement | null>(null)
let chart: echarts.EChartsType | null = null
let resizeObserver: ResizeObserver | null = null

const hasData = computed(() => {
  return props.data && props.data.dates && props.data.dates.length > 0
})

const currentMMinus = computed(() => {
  if (!hasData.value) return 0
  return props.data!.m_minus[props.data!.m_minus.length - 1]
})

const currentMPlus = computed(() => {
  if (!hasData.value) return 0
  return props.data!.m_plus[props.data!.m_plus.length - 1]
})

const nEffective = computed(() => {
  if (!hasData.value) return 1
  return props.data!.n_effective[props.data!.n_effective.length - 1] || 1
})

// 预警阈值: m-/n > 0.5 (论文中 SPsector n=10 用 m->=5)
const alertThreshold = computed(() => 0.5)

const mMinusClass = computed(() => {
  const ratio = currentMMinus.value / nEffective.value
  if (ratio > alertThreshold.value) return 'danger'
  if (ratio > alertThreshold.value * 0.6) return 'warning'
  return 'good'
})

const ratioClass = computed(() => mMinusClass.value)

const thresholdHint = computed(() =>
  `m⁻/n 比率。当前阈值 ${alertThreshold.value}：超过此值表示分散化严重失效。` +
  `论文实证：S&P500 10 部门 (n=10) 在 2008 危机、欧债危机、COVID-19 期间 m⁻ 峰值达 5-8。`
)

function buildOption() {
  if (!hasData.value) return {}
  const d = props.data!
  const n = nEffective.value

  // m-/n 比率序列
  const ratioSeries = d.m_minus.map(v => v / n)

  return {
    backgroundColor: 'transparent',
    tooltip: {
      trigger: 'axis',
      axisPointer: { type: 'cross' },
      formatter: (params: any) => {
        const idx = params[0]?.dataIndex ?? 0
        const date = d.dates[idx] || ''
        const mPlus = d.m_plus[idx] ?? 0
        const mMinus = d.m_minus[idx] ?? 0
        const ratio = (mMinus / n).toFixed(3)
        return `${date}<br/>` +
          `<span style="color:#ef232a">m⁻ = ${mMinus}</span><br/>` +
          `<span style="color:#2962ff">m⁺ = ${mPlus}</span><br/>` +
          `m⁻/n = ${ratio}`
      }
    },
    legend: {
      data: ['m⁻ (下界外)', 'm⁺ (上界外)', 'm⁻/n 比率'],
      top: 4,
      textStyle: { color: '#ccc', fontSize: 11 }
    },
    grid: [
      { left: 50, right: 50, top: 40, height: '55%' },  // 上: m+/m-
      { left: 50, right: 50, top: '72%', height: '22%' }  // 下: m-/n 比率
    ],
    xAxis: [
      {
        type: 'category',
        data: d.dates,
        gridIndex: 0,
        axisLabel: { show: false },
        axisLine: { lineStyle: { color: '#444' } }
      },
      {
        type: 'category',
        data: d.dates,
        gridIndex: 1,
        axisLabel: { color: '#999', fontSize: 10, interval: Math.floor(d.dates.length / 6) },
        axisLine: { lineStyle: { color: '#444' } }
      }
    ],
    yAxis: [
      {
        type: 'value',
        name: 'm⁺ / m⁻',
        gridIndex: 0,
        nameTextStyle: { color: '#999', fontSize: 10 },
        axisLabel: { color: '#999', fontSize: 10 },
        splitLine: { lineStyle: { color: '#333' } },
        minInterval: 1
      },
      {
        type: 'value',
        name: 'm⁻/n',
        gridIndex: 1,
        nameTextStyle: { color: '#999', fontSize: 10 },
        axisLabel: { color: '#999', fontSize: 10 },
        splitLine: { lineStyle: { color: '#333' } },
        min: 0,
        max: 1
      }
    ],
    series: [
      {
        name: 'm⁻ (下界外)',
        type: 'line',
        xAxisIndex: 0,
        yAxisIndex: 0,
        data: d.m_minus,
        symbol: 'none',
        lineStyle: { color: '#ef232a', width: 1.5 },
        areaStyle: { color: 'rgba(239, 35, 42, 0.1)' }
      },
      {
        name: 'm⁺ (上界外)',
        type: 'line',
        xAxisIndex: 0,
        yAxisIndex: 0,
        data: d.m_plus,
        symbol: 'none',
        lineStyle: { color: '#2962ff', width: 1.5 }
      },
      {
        name: 'm⁻/n 比率',
        type: 'line',
        xAxisIndex: 1,
        yAxisIndex: 1,
        data: ratioSeries,
        symbol: 'none',
        lineStyle: { color: '#ff9800', width: 1.5 },
        areaStyle: {
          color: {
            type: 'linear', x: 0, y: 0, x2: 0, y2: 1,
            colorStops: [
              { offset: 0, color: 'rgba(255, 152, 0, 0.3)' },
              { offset: 1, color: 'rgba(255, 152, 0, 0.02)' }
            ]
          }
        },
        markLine: {
          silent: true,
          symbol: 'none',
          lineStyle: { color: '#ef232a', type: 'dashed', width: 1 },
          label: { formatter: '预警 {c}', color: '#ef232a', fontSize: 10 },
          data: [{ yAxis: alertThreshold.value }]
        }
      }
    ]
  }
}

function renderChart() {
  if (!hasData.value) return
  nextTick(() => {
    if (chartRef.value && !chart) {
      chart = echarts.init(chartRef.value)
    }
    chart?.setOption(buildOption(), true)
  })
}

watch(() => props.data, renderChart, { deep: true })

onMounted(() => {
  renderChart()
  resizeObserver = new ResizeObserver(() => chart?.resize())
  if (chartRef.value) resizeObserver.observe(chartRef.value)
})

onUnmounted(() => {
  resizeObserver?.disconnect()
  chart?.dispose()
})
</script>

<style scoped>
.spectrum-card {
  background: rgba(36, 46, 66, 0.6);
  border: 1px solid rgba(74, 85, 104, 0.3);
  border-radius: 8px;
  padding: 16px;
}

.card-title {
  margin: 0 0 12px 0;
  font-size: 14px;
  color: #e0e0e0;
  font-weight: 600;
  display: flex;
  align-items: center;
  gap: 6px;
}

.empty-hint {
  color: #666;
  font-size: 12px;
  text-align: center;
  padding: 20px;
}

.stats-grid {
  display: grid;
  grid-template-columns: repeat(3, 1fr);
  gap: 10px;
  margin-bottom: 14px;
}

.stat-item {
  display: flex;
  flex-direction: column;
  gap: 2px;
  padding: 6px 8px;
  background: rgba(255, 255, 255, 0.03);
  border-radius: 4px;
}

.stat-label {
  font-size: 11px;
  color: #888;
  display: flex;
  align-items: center;
  gap: 4px;
}

.stat-value {
  font-size: 16px;
  font-weight: 600;
  color: #e0e0e0;
}

.stat-value.good { color: #00c853; }
.stat-value.warning { color: #ff9800; }
.stat-value.danger { color: #ef232a; }
.stat-value.stat-dim {
  font-size: 12px;
  font-weight: 400;
  color: #888;
}

.chart-container {
  width: 100%;
  height: 320px;
}
</style>
