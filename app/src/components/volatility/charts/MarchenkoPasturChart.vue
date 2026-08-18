<template>
  <div class="mp-card">
    <h3 class="card-title">
      Marchenko-Pastur 信号/噪声分解
      <TipHint content="随机矩阵理论 (RMT)：若相关矩阵特征值服从 Marchenko-Pastur 分布，则为纯噪声。超出 λ₊ 上界的特征值代表真实相关结构（信号）。Q = T/N 为样本比，Q > 3 结果可信。" />
    </h3>

    <div v-if="!hasData" class="empty-hint">需要至少 3 个标的且 Q = T/N > 1</div>

    <template v-else>
      <!-- 统计量 -->
      <div class="stats-grid">
        <div class="stat-item">
          <span class="stat-label">
            Q (T/N)
            <TipHint content="样本比。Q > 3 时 MP 近似良好，Q < 2 结果不可靠" />
          </span>
          <span class="stat-value" :class="qClass">{{ Q.toFixed(2) }}</span>
        </div>
        <div class="stat-item">
          <span class="stat-label">
            λ₊ 上界
            <TipHint content="MP 理论上界 (1+1/√Q)²。经验特征值超过此值的为信号" />
          </span>
          <span class="stat-value">{{ lambdaPlus.toFixed(4) }}</span>
        </div>
        <div class="stat-item">
          <span class="stat-label">
            信号维度
            <TipHint content="特征值 > λ₊ 的数量。占比越低说明标的间越接近独立噪声" />
          </span>
          <span class="stat-value" :class="signalClass">{{ nSignal }} / {{ N }} <span class="stat-unit">({{ (100 * nSignal / N).toFixed(0) }}%)</span></span>
        </div>
        <div class="stat-item">
          <span class="stat-label">
            信号方差占比
            <TipHint content="信号特征值之和 / 全部特征值之和。越高说明系统性风险越强，分散化效果越差" />
          </span>
          <span class="stat-value" :class="signalClass">{{ (100 * signalVarRatio).toFixed(1) }}%</span>
        </div>
        <div class="stat-item">
          <span class="stat-label">
            λ_max / λ₊
            <TipHint content="最大特征值超出 MP 上界的倍数。越大说明第一主成分越强" />
          </span>
          <span class="stat-value">{{ lambdaRatio.toFixed(2) }}×</span>
        </div>
        <div class="stat-item">
          <span class="stat-label">
            N (标的数) / T (观测数)
          </span>
          <span class="stat-value stat-dim">{{ N }} / {{ T }}</span>
        </div>
      </div>

      <!-- 图表 -->
      <div class="charts-row">
        <div class="chart-half">
          <div ref="histRef" class="chart-container"></div>
        </div>
        <div class="chart-half">
          <div ref="screeRef" class="chart-container"></div>
        </div>
      </div>
    </template>
  </div>
</template>

<script setup lang="ts">
import { ref, computed, watch, onMounted, onUnmounted, nextTick } from 'vue'
import * as echarts from 'echarts'
import TipHint from '../../TipHint.vue'

const props = defineProps<{
  data: {
    eigenvalues: number[]
    num_observations?: number
  } | null
}>()

const histRef = ref<HTMLElement | null>(null)
const screeRef = ref<HTMLElement | null>(null)
let histChart: echarts.EChartsType | null = null
let screeChart: echarts.EChartsType | null = null
let resizeObserver: ResizeObserver | null = null

// === MP 理论计算 ===
const N = computed(() => props.data?.eigenvalues?.length || 0)
const T = computed(() => props.data?.num_observations || 0)
const Q = computed(() => (N.value > 0 && T.value > 0) ? T.value / N.value : 0)
const lambdaPlus = computed(() => {
  const q = Q.value
  return q > 0 ? Math.pow(1 + 1 / Math.sqrt(q), 2) : 0
})
const lambdaMinus = computed(() => {
  const q = Q.value
  return q > 0 ? Math.pow(1 - 1 / Math.sqrt(q), 2) : 0
})

const hasData = computed(() => {
  const evals = props.data?.eigenvalues
  return evals && evals.length >= 3 && Q.value > 1
})

// 信号/噪声分解
const nSignal = computed(() => {
  if (!hasData.value) return 0
  const lp = lambdaPlus.value
  return props.data!.eigenvalues.filter(v => v > lp).length
})

const signalVarRatio = computed(() => {
  if (!hasData.value) return 0
  const evals = props.data!.eigenvalues
  const total = evals.reduce((s, v) => s + v, 0)
  if (total <= 0) return 0
  const lp = lambdaPlus.value
  const signalSum = evals.filter(v => v > lp).reduce((s, v) => s + v, 0)
  return signalSum / total
})

const lambdaRatio = computed(() => {
  if (!hasData.value) return 0
  const evals = props.data!.eigenvalues
  return evals.length > 0 && lambdaPlus.value > 0 ? evals[0] / lambdaPlus.value : 0
})

const qClass = computed(() => Q.value >= 3 ? 'good' : Q.value >= 2 ? 'warning' : 'danger')
const signalClass = computed(() => {
  const ratio = signalVarRatio.value
  return ratio > 0.5 ? 'danger' : ratio > 0.2 ? 'warning' : 'good'
})

// === MP 理论 PDF ===
function mpPDF(x: number, q: number): number {
  const lp = Math.pow(1 + 1 / Math.sqrt(q), 2)
  const lm = Math.pow(1 - 1 / Math.sqrt(q), 2)
  if (x < lm || x > lp) return 0
  return Math.sqrt((lp - x) * (x - lm)) / (2 * Math.PI * x / q)
}

// === 图表渲染 ===
function buildHistOption() {
  if (!hasData.value) return {}
  const evals = props.data!.eigenvalues.filter(v => v > 0)
  const q = Q.value
  const lp = lambdaPlus.value
  const lm = lambdaMinus.value

  // 直方图 bin
  const maxVal = Math.max(...evals) * 1.1
  const minVal = Math.max(0, Math.min(...evals) * 0.9)
  const nBins = Math.min(30, Math.max(10, Math.ceil(Math.sqrt(evals.length))))
  const binWidth = (maxVal - minVal) / nBins
  const bins = new Array(nBins).fill(0)
  const binCenters: number[] = []
  for (let i = 0; i < nBins; i++) {
    binCenters.push(minVal + (i + 0.5) * binWidth)
  }
  for (const v of evals) {
    const idx = Math.min(Math.floor((v - minVal) / binWidth), nBins - 1)
    if (idx >= 0) bins[idx]++
  }
  // 归一化为密度
  const totalArea = evals.length * binWidth
  const density = bins.map(b => b / totalArea)

  // MP 理论 PDF 曲线
  const pdfPoints = 200
  const mpX: number[] = []
  const mpY: number[] = []
  for (let i = 0; i <= pdfPoints; i++) {
    const x = lm + (lp - lm) * i / pdfPoints
    mpX.push(x)
    mpY.push(mpPDF(x, q))
  }

  // 按区域着色柱状图
  const barData = binCenters.map((cx, i) => {
    const color = cx > lp ? 'rgba(239, 35, 42, 0.7)' :   // 信号区（红色）
                  cx < lm ? 'rgba(255, 152, 0, 0.7)' :   // 同步化区（橙色）
                  'rgba(41, 98, 255, 0.6)'                // 噪声区（蓝色）
    return {
      value: [cx, density[i]],
      itemStyle: { color }
    }
  })

  return {
    backgroundColor: 'transparent',
    title: { text: '经验谱 vs MP 理论分布', left: 'center', top: 4, textStyle: { color: '#ccc', fontSize: 12 } },
    tooltip: {
      trigger: 'axis',
      formatter: (params: any) => {
        const p = params[0]
        if (!p) return ''
        const x = p.value[0]
        const region = x > lp ? '🔴 信号区' : x < lm ? '🟠 同步化区' : '🔵 噪声区'
        return `λ = ${x.toFixed(4)}<br/>密度 = ${p.value[1].toFixed(4)}<br/>${region}`
      }
    },
    legend: {
      data: ['信号区 (>λ₊)', '噪声区 (λ₋~λ₊)', '同步化区 (<λ₋)', 'MP 理论 PDF'],
      top: 24,
      textStyle: { color: '#ccc', fontSize: 10 },
      itemWidth: 12,
      itemHeight: 8
    },
    grid: { left: 50, right: 20, top: 50, bottom: 30, containLabel: true },
    xAxis: {
      type: 'value',
      name: '特征值',
      nameTextStyle: { color: '#999', fontSize: 10 },
      axisLabel: { color: '#999', fontSize: 10 },
      axisLine: { lineStyle: { color: '#444' } },
      splitLine: { show: false }
    },
    yAxis: {
      type: 'value',
      name: '密度',
      nameTextStyle: { color: '#999', fontSize: 10 },
      axisLabel: { color: '#999', fontSize: 10 },
      splitLine: { lineStyle: { color: '#333' } }
    },
    series: [
      {
        name: '经验密度',
        type: 'bar',
        data: barData,
        barMaxWidth: 20,
      },
      {
        name: 'MP 理论 PDF',
        type: 'line',
        data: mpX.map((x, i) => [x, mpY[i]]),
        smooth: true,
        symbol: 'none',
        lineStyle: { color: '#ff9800', width: 2 },
      },
      // λ 标线
      {
        name: 'λ₊',
        type: 'line',
        markLine: {
          silent: true,
          symbol: 'none',
          lineStyle: { color: '#ef232a', type: 'dashed', width: 1.5 },
          label: { formatter: 'λ₊={c}', color: '#ef232a', fontSize: 10 },
          data: [{ xAxis: lp }]
        },
        data: []
      },
      // λ₋ 标线
      {
        name: 'λ',
        type: 'line',
        markLine: {
          silent: true,
          symbol: 'none',
          lineStyle: { color: '#ff9800', type: 'dashed', width: 1.5 },
          label: { formatter: 'λ₋={c}', color: '#ff9800', fontSize: 10 },
          data: [{ xAxis: lm }]
        },
        data: []
      }
    ]
  }
}

function buildScreeOption() {
  if (!hasData.value) return {}
  const evals = props.data!.eigenvalues.filter(v => v > 0)
  const lp = lambdaPlus.value
  const lm = lambdaMinus.value
  const count = evals.length

  // 三色柱状图
  const barData = evals.map((v) => ({
    value: v,
    itemStyle: {
      color: v > lp ? '#ef232a' :   // 信号区（红色）
             v < lm ? '#ff9800' :   // 同步化区（橙色）
             '#2962ff'              // 噪声区（蓝色）
    }
  }))

  // 累积方差占比
  const totalVar = evals.reduce((s, v) => s + v, 0)
  let cumSum = 0
  const cumVarRatio = evals.map(v => {
    cumSum += v
    return totalVar > 0 ? (cumSum / totalVar) * 100 : 0
  })

  return {
    backgroundColor: 'transparent',
    title: { text: 'Scree Plot (特征值降序)', left: 'center', top: 4, textStyle: { color: '#ccc', fontSize: 12 } },
    tooltip: {
      trigger: 'axis',
      axisPointer: { type: 'cross' },
      formatter: (params: any) => {
        const idx = params[0]?.dataIndex ?? 0
        const v = evals[idx] ?? 0
        const region = v > lp ? '🔴 信号' : v < lm ? '🟠 同步化' : '🔵 噪声'
        const cumRatio = cumVarRatio[idx]?.toFixed(1) ?? '0.0'
        return `λ<sub>${idx + 1}</sub> = ${v.toFixed(6)}<br/>${region}<br/>累积方差占比：${cumRatio}%`
      }
    },
    legend: {
      data: ['特征值', '累积方差占比'],
      top: 24,
      textStyle: { color: '#ccc', fontSize: 10 },
      itemWidth: 12,
      itemHeight: 8
    },
    grid: { left: 55, right: 50, top: 50, bottom: 30, containLabel: true },
    xAxis: {
      type: 'category',
      data: evals.map((_, i) => `${i + 1}`),
      axisLabel: { color: '#999', fontSize: 10, interval: Math.floor(count / 8) },
      axisLine: { lineStyle: { color: '#444' } },
      name: '序号',
      nameTextStyle: { color: '#999', fontSize: 10 }
    },
    yAxis: [
      {
        type: 'log',
        name: 'λ (log)',
        nameTextStyle: { color: '#999', fontSize: 10 },
        axisLabel: { color: '#999', fontSize: 10 },
        splitLine: { lineStyle: { color: '#333' } },
        min: (value: any) => Math.max(value.min * 0.5, 1e-10)
      },
      {
        type: 'value',
        name: '累积方差 (%)',
        nameTextStyle: { color: '#999', fontSize: 10 },
        axisLabel: { color: '#999', fontSize: 10 },
        splitLine: { show: false },
        min: 0,
        max: 100
      }
    ],
    series: [
      {
        name: '特征值',
        type: 'bar',
        yAxisIndex: 0,
        data: barData,
        barWidth: '60%'
      },
      {
        name: '累积方差占比',
        type: 'line',
        yAxisIndex: 1,
        data: cumVarRatio,
        smooth: true,
        symbol: 'none',
        lineStyle: { color: '#00c853', width: 2 },
        areaStyle: {
          color: {
            type: 'linear', x: 0, y: 0, x2: 0, y2: 1,
            colorStops: [
              { offset: 0, color: 'rgba(0, 200, 83, 0.2)' },
              { offset: 1, color: 'rgba(0, 200, 83, 0.02)' }
            ]
          }
        }
      },
      {
        name: 'λ₊',
        type: 'line',
        markLine: {
          silent: true,
          symbol: 'none',
          lineStyle: { color: '#ef232a', type: 'dashed', width: 1.5 },
          label: { formatter: 'λ', color: '#ef232a', fontSize: 10, position: 'end' },
          data: [{ yAxis: lp }]
        },
        data: []
      },
      {
        name: 'λ',
        type: 'line',
        markLine: {
          silent: true,
          symbol: 'none',
          lineStyle: { color: '#ff9800', type: 'dashed', width: 1.5 },
          label: { formatter: 'λ', color: '#ff9800', fontSize: 10, position: 'end' },
          data: [{ yAxis: lm }]
        },
        data: []
      }
    ]
  }
}

function disposeCharts() {
  histChart?.dispose()
  screeChart?.dispose()
  histChart = null
  screeChart = null
}

function initAndRender() {
  if (!hasData.value) return
  nextTick(() => {
    if (histRef.value && !histChart) {
      histChart = echarts.init(histRef.value)
    }
    if (screeRef.value && !screeChart) {
      screeChart = echarts.init(screeRef.value)
    }
    histChart?.setOption(buildHistOption(), true)
    screeChart?.setOption(buildScreeOption(), true)

    // v-if 重建 DOM 后重新 observe 新元素
    if (resizeObserver) {
      resizeObserver.disconnect()
      if (histRef.value) resizeObserver.observe(histRef.value)
      if (screeRef.value) resizeObserver.observe(screeRef.value)
    }
  })
}

watch(() => props.data, (newData, oldData) => {
  // data 从有→无时清理，无→有时重建
  const had = oldData && oldData.eigenvalues && oldData.eigenvalues.length >= 3
  const has = newData && newData.eigenvalues && newData.eigenvalues.length >= 3
  if (had && !has) disposeCharts()
  initAndRender()
}, { deep: true })

onMounted(() => {
  resizeObserver = new ResizeObserver(() => {
    histChart?.resize()
    screeChart?.resize()
  })
  initAndRender()
})

onUnmounted(() => {
  resizeObserver?.disconnect()
  disposeCharts()
})
</script>

<style scoped>
.mp-card {
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

.stat-unit {
  font-size: 12px;
  font-weight: 400;
  color: #888;
}

.stat-dim {
  font-size: 13px;
  font-weight: 400;
  color: #888;
}

.charts-row {
  display: flex;
  gap: 12px;
}

.chart-half {
  flex: 1;
  min-width: 0;
}

.chart-container {
  width: 100%;
  height: 360px;
}
</style>
