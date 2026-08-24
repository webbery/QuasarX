<template>
  <div class="heatmap-wrapper">
    <div class="heatmap-controls">
      <span class="control-label">聚合粒度</span>
      <select v-model="granularity" @change="render">
        <option value="day">日</option>
        <option value="week">周</option>
        <option value="month">月</option>
      </select>
    </div>
    <div ref="chartEl" class="chart-container"></div>
  </div>
</template>

<script setup lang="ts">
import { ref, onMounted, onBeforeUnmount, watch } from 'vue'
import * as echarts from 'echarts'
import type { ShapResult } from '../composables/useMLState'

const props = defineProps<{ data: ShapResult }>()

const granularity = ref<'day' | 'week' | 'month'>('month')
let chart: echarts.ECharts | null = null
const chartEl = ref<HTMLDivElement | null>(null)

function getDateKey(dateStr: string, gran: string): string {
  if (!dateStr) return 'unknown'
  const d = new Date(dateStr)
  if (isNaN(d.getTime())) return 'unknown'
  const Y = d.getFullYear()
  const M = String(d.getMonth() + 1).padStart(2, '0')
  if (gran === 'month') return `${Y}-${M}`
  if (gran === 'day') return dateStr
  // week: ISO week approx
  const jan1 = new Date(Y, 0, 1)
  const weekNum = Math.ceil(((d.getTime() - jan1.getTime()) / 86400000 + jan1.getDay() + 1) / 7)
  return `${Y}-W${String(weekNum).padStart(2, '0')}`
}

function render() {
  if (!chart) return
  const { features, shap, dates } = props.data
  if (!dates || dates.length === 0 || shap.length === 0) {
    chart.setOption({ title: { text: '无日期数据，无法生成时间热力图', left: 'center', textStyle: { color: '#94a3b8', fontSize: 13 } } }, true)
    return
  }

  const gran = granularity.value
  // 按 (timeBucket, featureIndex) 聚合 mean|SHAP|
  const bucketMap = new Map<string, Map<number, { sum: number; count: number }>>()
  const bucketSet = new Set<string>()

  for (let i = 0; i < shap.length; i++) {
    const key = getDateKey(dates[i], gran)
    bucketSet.add(key)
    if (!bucketMap.has(key)) bucketMap.set(key, new Map())
    const fMap = bucketMap.get(key)!
    for (let j = 0; j < features.length; j++) {
      const val = Math.abs(shap[i][j] ?? 0)
      const entry = fMap.get(j) || { sum: 0, count: 0 }
      entry.sum += val
      entry.count++
      fMap.set(j, entry)
    }
  }

  const buckets = Array.from(bucketSet).sort()
  // 构建热力图数据: [bucketIdx, featureIdx, value]
  const heatData: [number, number, number][] = []
  let maxVal = 0
  for (let bi = 0; bi < buckets.length; bi++) {
    const fMap = bucketMap.get(buckets[bi])!
    for (let fi = 0; fi < features.length; fi++) {
      const entry = fMap.get(fi)
      const mean = entry ? entry.sum / entry.count : 0
      heatData.push([bi, fi, Number(mean.toFixed(6))])
      if (mean > maxVal) maxVal = mean
    }
  }

  const opts = {
    backgroundColor: 'transparent',
    title: { text: 'SHAP 时间热力图', left: 'center', textStyle: { color: '#e2e8f0', fontSize: 13 } },
    tooltip: {
      backgroundColor: 'rgba(15,25,41,0.95)',
      borderColor: '#2b3a55',
      textStyle: { color: '#e0e0e0' },
      formatter: (p: any) => {
        const [bi, fi, val] = p.data
        return `${buckets[bi]}<br/>${features[fi]}: <b>${val.toFixed(4)}</b>`
      }
    },
    grid: { left: 120, right: 60, top: 50, bottom: 40 },
    xAxis: {
      type: 'category',
      data: buckets,
      axisLabel: { color: '#94a3b8', rotate: buckets.length > 12 ? 45 : 0, fontSize: 10 },
      splitLine: { show: false },
      axisLine: { lineStyle: { color: '#2b3a55' } }
    },
    yAxis: {
      type: 'category',
      data: features,
      inverse: true,
      axisLabel: { color: '#cbd5e1', fontSize: 11 },
      axisLine: { lineStyle: { color: '#2b3a55' } }
    },
    visualMap: {
      min: 0,
      max: maxVal || 1,
      calculable: true,
      orient: 'vertical',
      right: 0,
      top: 50,
      bottom: 40,
      itemHeight: 200,
      inRange: { color: ['#1a2236', '#1e4fd9', '#3b82f6', '#60a5fa', '#fbbf24', '#f97316'] },
      textStyle: { color: '#94a3b8' },
      formatter: (v: number) => v.toFixed(3)
    },
    series: [{
      type: 'heatmap',
      data: heatData,
      label: { show: false },
      emphasis: { itemStyle: { shadowBlur: 10, shadowColor: 'rgba(0,0,0,0.5)' } },
      itemStyle: { borderColor: '#0f1929', borderWidth: 1 }
    }]
  }
  chart.setOption(opts, true)
}

onMounted(() => {
  if (chartEl.value) {
    chart = echarts.init(chartEl.value)
    render()
  }
  window.addEventListener('resize', handleResize)
})

onBeforeUnmount(() => {
  window.removeEventListener('resize', handleResize)
  chart?.dispose()
  chart = null
})

function handleResize() { chart?.resize() }
watch(() => props.data, render, { deep: true })
</script>

<style scoped>
.heatmap-wrapper { width: 100%; }
.heatmap-controls {
  display: flex;
  align-items: center;
  gap: 8px;
  margin-bottom: 8px;
}
.control-label { color: #94a3b8; font-size: 12px; }
.heatmap-controls select {
  background: rgba(0,0,0,0.3);
  border: 1px solid #2b3a55;
  border-radius: 4px;
  color: #e2e8f0;
  padding: 3px 8px;
  font-size: 12px;
  outline: none;
  cursor: pointer;
}
.chart-container { width: 100%; height: 400px; }
</style>
