<template>
  <div class="return-distribution-wrapper">
    <!-- 分布选择 + 拟合信息 -->
    <div class="dist-toolbar">
      <div class="dist-select-group">
        <label class="dist-label">分布拟合:</label>
        <select v-model="selectedDist" class="dist-select">
          <option value="normal">正态分布</option>
          <option value="t">t 分布</option>
          <option value="laplace">Laplace 分布</option>
          <option value="all">全部对比</option>
        </select>
      </div>
      <div v-if="selectedDist !== 'all'" class="fit-stats">
        <span class="fit-stat" :title="'Kolmogorov-Smirnov 统计量（越小越好）'">
          KS: <b :class="ksClass(currentFit.ks)">{{ currentFit.ks.toFixed(4) }}</b>
        </span>
        <span class="fit-stat" :title="'Akaike Information Criterion（越小越好）'">
          AIC: <b>{{ currentFit.aic.toFixed(1) }}</b>
        </span>
        <span class="fit-stat" :title="'Bayesian Information Criterion（越小越好）'">
          BIC: <b>{{ currentFit.bic.toFixed(1) }}</b>
        </span>
        <span class="fit-stat" :title="'对数似然'">
          lnL: <b>{{ currentFit.logLik.toFixed(1) }}</b>
        </span>
      </div>
    </div>

    <!-- 图表 -->
    <div ref="chartRef" class="return-distribution-chart"></div>

    <!-- 拟合参数面板 -->
    <div v-if="selectedDist !== 'all'" class="param-panel">
      <div v-for="(val, key) in currentFit.params" :key="key" class="param-item">
        <span class="param-label">{{ key }}</span>
        <span class="param-value">{{ val }}</span>
      </div>
      <div class="param-item">
        <span class="param-label">N</span>
        <span class="param-value">{{ n }}</span>
      </div>
    </div>

    <!-- 全部对比表格 -->
    <div v-if="selectedDist === 'all' && allFits.length" class="compare-table">
      <div class="compare-title">分布拟合对比</div>
      <table>
        <thead>
          <tr>
            <th>分布</th>
            <th>参数</th>
            <th>KS 统计量 ↓</th>
            <th>AIC ↓</th>
            <th>BIC ↓</th>
            <th>对数似然</th>
            <th>判定</th>
          </tr>
        </thead>
        <tbody>
          <tr v-for="f in allFits" :key="f.name" :class="{ best: f.isBest }">
            <td class="dist-name">{{ f.label }}</td>
            <td class="param-cell">{{ f.paramStr }}</td>
            <td :class="ksClass(f.ks)">{{ f.ks.toFixed(4) }}</td>
            <td>{{ f.aic.toFixed(1) }}</td>
            <td>{{ f.bic.toFixed(1) }}</td>
            <td>{{ f.logLik.toFixed(2) }}</td>
            <td>
              <span v-if="f.isBest" class="best-badge">最优</span>
            </td>
          </tr>
        </tbody>
      </table>
      <div class="compare-note">
        <span>KS &lt; 0.02 优秀</span>
        <span>0.02 ~ 0.05 可接受</span>
        <span>&gt; 0.05 拟合较差</span>
        <span style="margin-left: auto; color: #666;">AIC/BIC 越小越好</span>
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, computed, watch, onMounted } from 'vue'
import * as echarts from 'echarts'
import { useECharts, createBaseChartOption } from '../../report/composables/useECharts'
import type { VolatilitySingleResult } from '../composables/useVolatilityState'

const props = defineProps<{
  data: VolatilitySingleResult | null
}>()

const selectedDist = ref<'normal' | 't' | 'laplace' | 'all'>('normal')
const { chartRef, initChart, updateChart } = useECharts(false)

// ============== 数学工具 ==============

function logGamma(x: number): number {
  // Lanczos 近似 (Stirling + 修正)
  const cof = [
    76.18009172947146, -86.50532032941677, 24.01409824083091,
    -1.231739572450155, 0.1208650973866179e-2, -0.5395239384953e-5
  ]
  let y = x, tmp = x + 5.5
  tmp -= (x + 0.5) * Math.log(tmp)
  let ser = 1.000000000190015
  for (let j = 0; j < 6; j++) ser += cof[j] / ++y
  return -tmp + Math.log(2.5066282746310005 * ser / x)
}

function normalPDF(x: number, mu: number, sigma: number): number {
  const z = (x - mu) / sigma
  return Math.exp(-0.5 * z * z) / (sigma * Math.sqrt(2 * Math.PI))
}

function normalCDF(x: number, mu: number, sigma: number): number {
  const z = (x - mu) / (sigma * Math.SQRT2)
  return 0.5 * (1 + erf(z))
}

function erf(x: number): number {
  // Abramowitz & Stegun 近似
  const a1 = 0.254829592, a2 = -0.284496736, a3 = 1.421413741
  const a4 = -1.453152027, a5 = 1.061405429, p = 0.3275911
  const sign = x >= 0 ? 1 : -1
  x = Math.abs(x)
  const t = 1.0 / (1.0 + p * x)
  const y = 1.0 - (((((a5 * t + a4) * t) + a3) * t + a2) * t + a1) * t * Math.exp(-x * x)
  return sign * y
}

function tPDF(x: number, nu: number, mu: number, sigma: number): number {
  const z = (x - mu) / sigma
  const logCoeff = logGamma((nu + 1) / 2) - logGamma(nu / 2) - 0.5 * Math.log(nu * Math.PI) - Math.log(sigma)
  const logBody = -((nu + 1) / 2) * Math.log(1 + z * z / nu)
  return Math.exp(logCoeff + logBody)
}

function tCDF(x: number, nu: number, mu: number, sigma: number): number {
  // 用正则化不完全 Beta 函数近似
  const z = (x - mu) / sigma
  const t = nu / (nu + z * z)
  if (z >= 0) {
    return 1 - 0.5 * regIncBeta(nu / 2, 0.5, t)
  }
  return 0.5 * regIncBeta(nu / 2, 0.5, t)
}

function regIncBeta(a: number, b: number, x: number): number {
  // 连分数近似 (Lentz's method)
  if (x <= 0) return 0
  if (x >= 1) return 1
  const logBeta = logGamma(a) + logGamma(b) - logGamma(a + b)
  const front = Math.exp(a * Math.log(x) + b * Math.log(1 - x) - logBeta) / a
  // 连分数
  let f = 1, c = 1, d = 1 - (a + b) * x / (a + 1)
  if (Math.abs(d) < 1e-30) d = 1e-30
  d = 1 / d
  f = d
  for (let m = 1; m <= 200; m++) {
    // even step
    let num = m * (b - m) * x / ((a + 2 * m - 1) * (a + 2 * m))
    d = 1 + num * d; if (Math.abs(d) < 1e-30) d = 1e-30; d = 1 / d
    c = 1 + num / c; if (Math.abs(c) < 1e-30) c = 1e-30
    f *= d * c
    // odd step
    num = -(a + m) * (a + b + m) * x / ((a + 2 * m) * (a + 2 * m + 1))
    d = 1 + num * d; if (Math.abs(d) < 1e-30) d = 1e-30; d = 1 / d
    c = 1 + num / c; if (Math.abs(c) < 1e-30) c = 1e-30
    const delta = d * c
    f *= delta
    if (Math.abs(delta - 1) < 1e-10) break
  }
  return front * f
}

function laplacePDF(x: number, mu: number, b: number): number {
  return Math.exp(-Math.abs(x - mu) / b) / (2 * b)
}

function laplaceCDF(x: number, mu: number, b: number): number {
  if (x < mu) return 0.5 * Math.exp((x - mu) / b)
  return 1 - 0.5 * Math.exp(-(x - mu) / b)
}

// ============== 分布拟合 ==============

interface FitResult {
  name: string
  label: string
  params: Record<string, string>
  paramStr: string
  pdf: (x: number) => number
  cdf: (x: number) => number
  k: number  // 参数个数
  logLik: number
  aic: number
  bic: number
  ks: number
  isBest: boolean
}

function fitDistribution(returns: number[], type: 'normal' | 't' | 'laplace'): FitResult {
  const n = returns.length
  const mean = returns.reduce((a, b) => a + b, 0) / n
  const variance = returns.reduce((a, b) => a + (b - mean) ** 2, 0) / (n - 1)
  const std = Math.sqrt(variance)
  const excessKurtosis = returns.reduce((a, b) => a + ((b - mean) / std) ** 4, 0) / n - 3

  let pdf: (x: number) => number
  let cdf: (x: number) => number
  let k: number
  let params: Record<string, string>
  let label: string

  switch (type) {
    case 'normal': {
      pdf = (x) => normalPDF(x, mean, std)
      cdf = (x) => normalCDF(x, mean, std)
      k = 2
      params = { 'μ': (mean * 100).toFixed(4) + '%', 'σ': (std * 100).toFixed(4) + '%' }
      label = '正态'
      break
    }
    case 't': {
      // 方法矩估计: ν = 4 + 6/κ (κ = excess kurtosis > 0)
      let nu = excessKurtosis > 0.1 ? 4 + 6 / excessKurtosis : 30
      nu = Math.max(nu, 2.5)
      // σ_t: variance = σ² × ν/(ν-2) → σ = std × √((ν-2)/ν)
      const sigmaT = std * Math.sqrt((nu - 2) / nu)
      pdf = (x) => tPDF(x, nu, mean, sigmaT)
      cdf = (x) => tCDF(x, nu, mean, sigmaT)
      k = 3
      params = { 'μ': (mean * 100).toFixed(4) + '%', 'σ': (sigmaT * 100).toFixed(4) + '%', 'ν': nu.toFixed(2) }
      label = 't'
      break
    }
    case 'laplace': {
      // MLE: b = mean(|x - μ|)
      const b = returns.reduce((a, r) => a + Math.abs(r - mean), 0) / n
      pdf = (x) => laplacePDF(x, mean, b)
      cdf = (x) => laplaceCDF(x, mean, b)
      k = 2
      params = { 'μ': (mean * 100).toFixed(4) + '%', 'b': (b * 100).toFixed(4) + '%' }
      label = 'Laplace'
      break
    }
  }

  // 对数似然
  let logLik = 0
  for (const r of returns) {
    const p = pdf(r)
    if (p > 0) logLik += Math.log(p)
  }

  // AIC = 2k - 2lnL, BIC = k*ln(n) - 2lnL
  const aic = 2 * k - 2 * logLik
  const bic = k * Math.log(n) - 2 * logLik

  // KS 统计量
  const sorted = [...returns].sort((a, b) => a - b)
  let ks = 0
  for (let i = 0; i < sorted.length; i++) {
    const empCDFLow = i / n
    const empCDFHigh = (i + 1) / n
    const theoCDF = cdf(sorted[i])
    ks = Math.max(ks, Math.abs(empCDFLow - theoCDF), Math.abs(empCDFHigh - theoCDF))
  }

  const paramStr = Object.entries(params).map(([k, v]) => `${k}=${v}`).join(', ')

  return { name: type, label, params, paramStr, pdf, cdf, k, logLik, aic, bic, ks, isBest: false }
}

// ============== 图表构建 ==============

const n = computed(() => props.data?.returns?.length || 0)

const currentFit = computed(() => {
  if (!props.data?.returns || selectedDist.value === 'all') {
    return { name: '', params: {} as Record<string, string>, ks: 0, aic: 0, bic: 0, logLik: 0 }
  }
  return fitDistribution(props.data.returns, selectedDist.value)
})

const allFits = computed(() => {
  if (!props.data?.returns) return []
  const returns = props.data.returns
  const fits = ['normal', 't', 'laplace'].map(t => fitDistribution(returns, t as any))
  // 标记 AIC 最优
  const bestAIC = Math.min(...fits.map(f => f.aic))
  fits.forEach(f => f.isBest = f.aic === bestAIC)
  return fits
})

function ksClass(ks: number) {
  if (ks < 0.02) return 'ks-good'
  if (ks < 0.05) return 'ks-warn'
  return 'ks-bad'
}

function buildOption() {
  if (!props.data || !props.data.returns) return {}

  const returns = props.data.returns
  const nRet = returns.length
  const bins = Math.min(50, Math.max(20, Math.ceil(Math.sqrt(nRet))))
  const min = Math.min(...returns)
  const max = Math.max(...returns)
  const range = max - min
  const binWidth = range / bins
  const padding = range * 0.05

  // 直方图
  const histBins: number[] = []
  const histCounts: number[] = []
  for (let i = 0; i < bins; i++) {
    const low = min + i * binWidth
    histBins.push(low + binWidth / 2)
    const count = returns.filter(r => r >= low && (i === bins - 1 ? r <= low + binWidth : r < low + binWidth)).length
    histCounts.push(count)
  }

  // 拟合曲线（缩放到频数空间，返回 [x, y] 对）
  function fitToCountsPairs(fit: FitResult): [number, number][] {
    return histBins.map(x => [x, fit.pdf(x) * nRet * binWidth])
  }

  const series: any[] = [
    {
      name: '频数',
      type: 'bar',
      data: histBins.map((x, i) => [x, histCounts[i]]),
      itemStyle: { color: 'rgba(41, 98, 255, 0.5)' },
      barWidth: '90%',
      xAxisIndex: 0,
      yAxisIndex: 0
    }
  ]

  const colors: Record<string, string> = { normal: '#ff9800', t: '#e91e63', laplace: '#00c853' }
  const distNames: Record<string, string> = { normal: '正态拟合', t: 't 拟合', laplace: 'Laplace 拟合' }

  if (selectedDist.value === 'all') {
    for (const fit of allFits.value) {
      series.push({
        name: distNames[fit.name],
        type: 'line',
        data: fitToCountsPairs(fit),
        smooth: true,
        symbol: 'none',
        lineStyle: { color: colors[fit.name], width: 2 },
        xAxisIndex: 0,
        yAxisIndex: 0
      })
    }
  } else {
    const fit = currentFit.value as FitResult
    series.push({
      name: distNames[selectedDist.value],
      type: 'line',
      data: fitToCountsPairs(fit),
      smooth: true,
      symbol: 'none',
      lineStyle: { color: colors[selectedDist.value], width: 2.5 },
      xAxisIndex: 0,
      yAxisIndex: 0
    })
  }

  // 使用 value 轴以便曲线对齐
  const xMin = min - padding
  const xMax = max + padding

  return createBaseChartOption({
    title: {
      text: '收益率分布',
      left: 'center',
      top: 4,
      textStyle: { color: '#e0e0e0', fontSize: 13 }
    },
    legend: {
      top: 24,
      textStyle: { color: '#999', fontSize: 11 },
      itemWidth: 14,
      itemHeight: 8
    },
    tooltip: {
      trigger: 'axis',
      formatter: (params: any[]) => {
        const x = params[0].value[0] ?? params[0].data
        let s = `x = ${(x * 100).toFixed(3)}%<br/>`
        for (const p of params) {
          const v = Array.isArray(p.value) ? p.value[1] : p.value
          s += `${p.marker} ${p.seriesName}: ${typeof v === 'number' ? v.toFixed(1) : v}<br/>`
        }
        return s
      }
    },
    grid: { left: 50, right: 20, top: 60, bottom: 30, containLabel: true },
    xAxis: {
      type: 'value',
      name: '收益率',
      nameTextStyle: { color: '#999', fontSize: 10 },
      axisLabel: {
        color: '#999',
        fontSize: 10,
        formatter: (v: number) => (v * 100).toFixed(1) + '%'
      },
      axisLine: { lineStyle: { color: '#444' } },
      splitLine: { show: false },
      min: xMin,
      max: xMax
    },
    yAxis: {
      type: 'value',
      name: '频数',
      axisLabel: { color: '#999', fontSize: 10 },
      nameTextStyle: { color: '#999', fontSize: 10 },
      splitLine: { lineStyle: { color: '#333' } }
    },
    series
  })
}

watch(() => [props.data, selectedDist.value], () => {
  if (props.data && chartRef.value) {
    if (!echarts.getInstanceByDom(chartRef.value)) {
      initChart()
    }
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
.return-distribution-wrapper {
  display: flex;
  flex-direction: column;
  height: 100%;
  min-height: 300px;
}

.dist-toolbar {
  display: flex;
  align-items: center;
  justify-content: space-between;
  flex-wrap: wrap;
  gap: 8px;
  padding: 0 4px 6px;
}

.dist-select-group {
  display: flex;
  align-items: center;
  gap: 6px;
}

.dist-label {
  font-size: 12px;
  color: #999;
}

.dist-select {
  background: rgba(255, 255, 255, 0.06);
  border: 1px solid rgba(74, 85, 104, 0.4);
  border-radius: 4px;
  color: #e0e0e0;
  font-size: 12px;
  padding: 3px 8px;
  cursor: pointer;
  outline: none;
}
.dist-select:focus {
  border-color: #2962ff;
}

.fit-stats {
  display: flex;
  gap: 12px;
  flex-wrap: wrap;
}

.fit-stat {
  font-size: 11px;
  color: #888;
}
.fit-stat b {
  color: #ccc;
  font-weight: 600;
}
.fit-stat b.ks-good { color: #00c853; }
.fit-stat b.ks-warn { color: #ff9800; }
.fit-stat b.ks-bad { color: #ef232a; }

.return-distribution-chart {
  flex: 1;
  min-height: 160px;
}

.param-panel {
  display: flex;
  gap: 16px;
  padding: 6px 4px 0;
  flex-wrap: wrap;
}

.param-item {
  font-size: 11px;
}
.param-label {
  color: #777;
  margin-right: 4px;
}
.param-value {
  color: #ccc;
  font-weight: 500;
}

.compare-table {
  padding: 8px 4px 4px;
  overflow-x: auto;
}

.compare-title {
  font-size: 13px;
  font-weight: 600;
  color: #ccc;
  margin-bottom: 8px;
  padding-left: 4px;
}

.compare-table table {
  width: 100%;
  border-collapse: collapse;
  font-size: 13px;
}

.compare-table th {
  color: #999;
  font-weight: 500;
  text-align: left;
  padding: 6px 10px;
  border-bottom: 1px solid rgba(74, 85, 104, 0.4);
  font-size: 12px;
  white-space: nowrap;
}

.compare-table td {
  color: #ddd;
  padding: 7px 10px;
  border-bottom: 1px solid rgba(74, 85, 104, 0.2);
  font-size: 13px;
}

.compare-table .dist-name {
  font-weight: 600;
}

.compare-table tr.best {
  background: rgba(41, 98, 255, 0.1);
}

.compare-table .param-cell {
  font-size: 12px;
  color: #aaa;
}

.best-badge {
  display: inline-block;
  background: rgba(41, 98, 255, 0.2);
  color: #5b8def;
  font-size: 11px;
  font-weight: 600;
  padding: 2px 8px;
  border-radius: 3px;
}

.compare-note {
  display: flex;
  gap: 16px;
  margin-top: 8px;
  font-size: 11px;
  color: #777;
  flex-wrap: wrap;
}

.compare-table td.ks-good { color: #00c853; font-weight: 600; }
.compare-table td.ks-warn { color: #ff9800; font-weight: 600; }
.compare-table td.ks-bad { color: #ef232a; font-weight: 600; }
</style>
