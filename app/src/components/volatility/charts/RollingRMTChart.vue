<template>
  <div class="rmt-card">
    <h3 class="card-title">
      滚动 RMT 分析
      <TipHint content="滚动窗口下的 RMT 统计量演变。m⁻/n 比率突增预示分散化失效；信号方差占比上升表示系统性风险增强；λ_max/λ₊ 倍数反映主导因子强度。" />
    </h3>

    <div v-if="!hasData" class="empty-hint">
      需要至少 2 个标的且数据长度 > 滚动窗口 ({{ data?.window_size || 60 }})
    </div>

    <template v-else>
      <!-- 统计面板 -->
      <div class="stats-grid">
        <div class="stat-item">
          <span class="stat-label">当前 m⁻/n</span>
          <span class="stat-value" :class="ratioClass">{{ currentRatio.toFixed(3) }}</span>
        </div>
        <div class="stat-item">
          <span class="stat-label">当前信号方差占比</span>
          <span class="stat-value" :class="signalClass">{{ (currentSignalVar * 100).toFixed(1) }}%</span>
        </div>
        <div class="stat-item">
          <span class="stat-label">当前 λ_max/λ₊</span>
          <span class="stat-value">{{ currentLambdaRatio.toFixed(2) }}×</span>
        </div>
        <div class="stat-item">
          <span class="stat-label">预警阈值</span>
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
  signal_var_ratio: number[]
  lambda_max: number[]
  lambda_max_ratio: number[]
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

const nEffective = computed(() => {
  if (!hasData.value) return 1
  return props.data!.n_effective[props.data!.n_effective.length - 1] || 1
})

const currentRatio = computed(() => {
  if (!hasData.value) return 0
  const d = props.data!
  const idx = d.m_minus.length - 1
  return d.m_minus[idx] / (d.n_effective[idx] || 1)
})

const currentSignalVar = computed(() => {
  if (!hasData.value) return 0
  return props.data!.signal_var_ratio[props.data!.signal_var_ratio.length - 1] || 0
})

const currentLambdaRatio = computed(() => {
  if (!hasData.value) return 0
  return props.data!.lambda_max_ratio[props.data!.lambda_max_ratio.length - 1] || 0
})

const alertThreshold = computed(() => 0.5)

const ratioClass = computed(() => {
  const ratio = currentRatio.value
  if (ratio > alertThreshold.value) return 'danger'
  if (ratio > alertThreshold.value * 0.6) return 'warning'
  return 'good'
})

const signalClass = computed(() => {
  const ratio = currentSignalVar.value
  if (ratio > 0.5) return 'danger'
  if (ratio > 0.2) return 'warning'
  return 'good'
})

function buildOption() {
  if (!hasData.value) return {}
  const d = props.data!
  const n = nEffective.value

  // m-/n 比率序列
  const ratioSeries = d.m_minus.map((v, i) => v / (d.n_effective[i] || 1))

  return {
    backgroundColor: 'transparent',
    tooltip: {
      trigger: 'axis',
      axisPointer: { type: 'cross' },
      formatter: (params: any) => {
        const idx = params[0]?.dataIndex ?? 0
        const date = d.dates[idx] || ''
        const mMinus = d.m_minus[idx] ?? 0
        const ratio = ratioSeries[idx] ?? 0
        const sigVar = (d.signal_var_ratio[idx] ?? 0) * 100
        const lamRatio = d.lambda_max_ratio[idx] ?? 0
        return `${date}<br/>` +
          `<span style="color:#ef232a">m⁻/n = ${ratio.toFixed(3)}</span><br/>` +
          `<span style="color:#2962ff">信号方差占比 = ${sigVar.toFixed(1)}%</span><br/>` +
          `<span style="color:#00c853">λ_max/λ₊ = ${lamRatio.toFixed(2)}×</span>`
      }
    },
    legend: {
      data: ['m⁻/n 比率', '信号方差占比 (%)', 'λ_max/λ₊ 倍数'],
      top: 4,
      textStyle: { color: '#ccc', fontSize: 11 }
    },
    grid: [
      { left: 50, right: 50, top: 40, height: '28%' },
      { left: 50, right: 50, top: '38%', height: '28%' },
      { left: 50, right: 50, top: '76%', height: '22%' }
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
        axisLabel: { show: false },
        axisLine: { lineStyle: { color: '#444' } }
      },
      {
        type: 'category',
        data: d.dates,
        gridIndex: 2,
        axisLabel: { color: '#999', fontSize: 10, interval: Math.floor(d.dates.length / 6) },
        axisLine: { lineStyle: { color: '#444' } }
      }
    ],
    yAxis: [
      {
        type: 'value',
        name: 'm⁻/n',
        gridIndex: 0,
        nameTextStyle: { color: '#999', fontSize: 10 },
        axisLabel: { color: '#999', fontSize: 10 },
        splitLine: { lineStyle: { color: '#333' } },
        min: 0,
        max: 1
      },
      {
        type: 'value',
        name: '信号方差 (%)',
        gridIndex: 1,
        nameTextStyle: { color: '#999', fontSize: 10 },
        axisLabel: { color: '#999', fontSize: 10 },
        splitLine: { lineStyle: { color: '#333' } },
        min: 0,
        max: 100
      },
      {
        type: 'value',
        name: 'λ_max/λ₊',
        gridIndex: 2,
        nameTextStyle: { color: '#999', fontSize: 10 },
        axisLabel: { color: '#999', fontSize: 10 },
        splitLine: { lineStyle: { color: '#333' } },
        min: 0
      }
    ],
    series: [
      {
        name: 'm⁻/n 比率',
        type: 'line',
        xAxisIndex: 0,
        yAxisIndex: 0,
        data: ratioSeries,
        symbol: 'none',
        lineStyle: { color: '#ef232a', width: 1.5 },
        areaStyle: {
          color: {
            type: 'linear', x: 0, y: 0, x2: 0, y2: 1,
            colorStops: [
              { offset: 0, color: 'rgba(239, 35, 42, 0.3)' },
              { offset: 1, color: 'rgba(239, 35, 42, 0.02)' }
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
      },
      {
        name: '信号方差占比 (%)',
        type: 'line',
        xAxisIndex: 1,
        yAxisIndex: 1,
        data: d.signal_var_ratio.map(v => v * 100),
        symbol: 'none',
        lineStyle: { color: '#2962ff', width: 1.5 },
        areaStyle: {
          color: {
            type: 'linear', x: 0, y: 0, x2: 0, y2: 1,
            colorStops: [
              { offset: 0, color: 'rgba(41, 98, 255, 0.3)' },
              { offset: 1, color: 'rgba(41, 98, 255, 0.02)' }
            ]
          }
        }
      },
      {
        name: 'λ_max/λ₊ 倍数',
        type: 'line',
        xAxisIndex: 2,
        yAxisIndex: 2,
        data: d.lambda_max_ratio,
        symbol: 'none',
        lineStyle: { color: '#00c853', width: 1.5 },
        areaStyle: {
          color: {
            type: 'linear', x: 0, y: 0, x2: 0, y2: 1,
            colorStops: [
              { offset: 0, color: 'rgba(0, 200, 83, 0.3)' },
              { offset: 1, color: 'rgba(0, 200, 83, 0.02)' }
            ]
          }
        },
        markLine: {
          silent: true,
          symbol: 'none',
          lineStyle: { color: '#999', type: 'dotted', width: 1 },
          label: { formatter: 'λ基准 {c}', color: '#999', fontSize: 9 },
          data: [{ yAxis: 1.0 }]
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
.rmt-card {
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
  height: 400px;
}
</style>
