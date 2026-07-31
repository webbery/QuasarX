<template>
  <div class="loss-curve">
    <div class="curve-toolbar">
      <span class="loss-type-tag" :class="lossType">{{ lossTypeLabel }}</span>
      <span v-if="showOverfitWarn" class="overfit-warn">
        <i class="fas fa-exclamation-triangle"></i>疑似过拟合
      </span>
      <div class="scale-toggle">
        <button :class="{ active: !logScale }" @click="logScale = false">线性</button>
        <button :class="{ active: logScale }" @click="logScale = true">对数</button>
      </div>
    </div>
    <div ref="chartEl" class="chart-container"></div>
  </div>
</template>

<script setup lang="ts">
import { computed, onBeforeUnmount, onMounted, ref, watch } from 'vue'
import * as echarts from 'echarts'
import type { LearningCurvePoint } from '../composables/useMLState'

const props = defineProps<{
  data: LearningCurvePoint[]
  bestIteration?: number
  objective?: string
}>()

const chartEl = ref<HTMLDivElement | null>(null)
let chart: echarts.ECharts | null = null
const logScale = ref(false)

const lossType = computed(() => {
  const obj = props.objective || ''
  if (obj === 'binary:logistic') return 'logloss'
  if (obj === 'multi:softprob') return 'mlogloss'
  if (obj === 'reg:squarederror') return 'rmse'
  return ''
})

const lossTypeLabel = computed(() => {
  const t = lossType.value
  if (!t) return ''
  return t === 'mlogloss' ? 'Multi LogLoss' : t.toUpperCase()
})

const showOverfitWarn = computed(() => {
  const last = props.data[props.data.length - 1]
  if (!last || last.train_loss <= 0) return false
  return last.eval_loss > last.train_loss * 1.5
})

const hasValidData = computed(() => {
  return props.data.some(d => d.train_loss > 0 && d.eval_loss > 0)
})

function render() {
  if (!chart || props.data.length === 0) return
  const useLog = logScale.value && hasValidData.value

  const markLineData = (typeof props.bestIteration === 'number' && props.bestIteration > 0)
    ? [{ xAxis: props.bestIteration }]
    : []

  const opts = {
    backgroundColor: 'transparent',
    tooltip: {
      trigger: 'axis',
      backgroundColor: 'rgba(15,25,41,0.95)',
      borderColor: '#2b3a55',
      textStyle: { color: '#e0e0e0' },
      formatter: (params: any[]) => {
        const iter = params[0]?.axisValue ?? ''
        const train = params.find((p: any) => p.seriesName === 'train_loss')
        const evalL = params.find((p: any) => p.seriesName === 'eval_loss')
        const fmt = (v: any) => (typeof v === 'number' ? v.toFixed(4) : '-')
        let html = `<div style="font-size:11px;color:#94a3b8">iter ${iter}</div>`
        if (train) html += `<div style="color:#5470c6">train: ${fmt(train.data)}</div>`
        if (evalL) html += `<div style="color:#ee6666">eval:  ${fmt(evalL.data)}</div>`
        return html
      },
    },
    legend: { data: ['train_loss', 'eval_loss'], top: 0, textStyle: { color: '#94a3b8' } },
    grid: { left: 56, right: 24, top: 36, bottom: 60 },
    xAxis: {
      type: 'category',
      data: props.data.map(d => d.iteration),
      name: 'Iteration',
      nameLocation: 'middle',
      nameGap: 28,
      nameTextStyle: { color: '#94a3b8', fontSize: 11 },
      axisLabel: { color: '#94a3b8' },
      axisLine: { lineStyle: { color: '#2b3a55' } },
    },
    yAxis: {
      type: useLog ? 'log' : 'value',
      name: 'Loss',
      nameTextStyle: { color: '#94a3b8' },
      axisLabel: { color: '#94a3b8' },
      splitLine: { lineStyle: { color: 'rgba(255,255,255,0.05)' } },
      ...(useLog ? { min: 'dataMin', max: 'dataMax' } : { scale: true }),
    },
    dataZoom: [
      { type: 'inside', xAxisIndex: 0 },
      { type: 'slider', xAxisIndex: 0, height: 16, bottom: 8, borderColor: '#2b3a55',
        fillerColor: 'rgba(91,143,249,0.15)', handleStyle: { color: '#5b8ff9' } },
    ],
    series: [
      {
        name: 'train_loss',
        type: 'line',
        smooth: true,
        showSymbol: false,
        data: props.data.map(d => Number(d.train_loss.toFixed(4))),
        itemStyle: { color: '#5470c6' },
        lineStyle: { width: 1.6 },
        markLine: markLineData.length > 0 ? {
          silent: true,
          symbol: 'none',
          lineStyle: { type: 'dashed', color: '#fbbf24', width: 1.5 },
          label: {
            formatter: `best: ${props.bestIteration}`,
            position: 'end',
            color: '#fbbf24',
            fontSize: 11,
            backgroundColor: 'rgba(15,25,41,0.85)',
            padding: [2, 4],
            borderRadius: 3,
          },
          data: markLineData,
        } : undefined,
      },
      {
        name: 'eval_loss',
        type: 'line',
        smooth: true,
        showSymbol: false,
        data: props.data.map(d => Number(d.eval_loss.toFixed(4))),
        itemStyle: { color: '#ee6666' },
        lineStyle: { width: 1.6 },
      },
    ],
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

watch(() => [props.data, props.bestIteration, props.objective, logScale.value], render, { deep: true })
</script>

<style scoped>
.loss-curve { padding: 4px 0; }
.curve-toolbar {
  display: flex;
  align-items: center;
  gap: 10px;
  padding: 0 4px 8px;
  font-size: 11px;
}
.loss-type-tag {
  display: inline-block;
  padding: 2px 8px;
  border-radius: 999px;
  font-weight: 600;
  letter-spacing: 0.4px;
  font-family: 'SF Mono', 'Consolas', monospace;
}
.loss-type-tag.logloss  { background: rgba(91,143,249,0.18); color: #5b8ff9; border: 1px solid rgba(91,143,249,0.35); }
.loss-type-tag.mlogloss { background: rgba(168,85,247,0.18); color: #c084fc; border: 1px solid rgba(168,85,247,0.35); }
.loss-type-tag.rmse     { background: rgba(45,212,191,0.18); color: #2dd4bf; border: 1px solid rgba(45,212,191,0.35); }
.overfit-warn {
  display: inline-flex;
  align-items: center;
  gap: 4px;
  padding: 2px 8px;
  background: rgba(248,113,113,0.15);
  color: #f87171;
  border: 1px solid rgba(248,113,113,0.4);
  border-radius: 999px;
  font-weight: 600;
}
.scale-toggle {
  margin-left: auto;
  display: inline-flex;
  background: #0f1929;
  border: 1px solid #2b3a55;
  border-radius: 4px;
  overflow: hidden;
}
.scale-toggle button {
  background: transparent;
  color: #94a3b8;
  border: none;
  padding: 3px 10px;
  font-size: 11px;
  cursor: pointer;
}
.scale-toggle button.active {
  background: #3b4a6b;
  color: #f1f5f9;
}
.chart-container { width: 100%; height: 340px; }
</style>