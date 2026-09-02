<template>
  <div class="iv-surface-chart">
    <div class="toolbar">
      <div class="form-row">
        <label>交易所</label>
        <select v-model="exchange" @change="loadSurface">
          <option value="CFFEX">CFFEX</option>
          <option value="SSE">SSE</option>
          <option value="SZSE">SZSE</option>
        </select>
      </div>
      <div class="form-row">
        <label>标的</label>
        <select v-model="product" @change="loadSurface">
          <option value="50ETF">50ETF</option>
          <option value="300ETF">300ETF</option>
          <option value="500ETF">500ETF</option>
          <option value="IO">IO</option>
          <option value="HO">HO</option>
          <option value="MO">MO</option>
        </select>
      </div>
      <button class="btn-toggle" @click="viewMode = viewMode === '3d' ? '2d' : '3d'">
        {{ viewMode === '3d' ? '切换 2D' : '切换 3D' }}
      </button>
    </div>
    <div v-if="loading" class="loading-hint">加载 IV 数据中...</div>
    <div v-else-if="error" class="error-hint">{{ error }}</div>
    <div v-else ref="chartRef" class="chart-area"></div>
  </div>
</template>

<script setup lang="ts">
import { ref, watch, onMounted, onUnmounted } from 'vue'
import * as echarts from 'echarts'
import 'echarts-gl'
import { getIVSurface, type IVSurfaceResult } from '../composables/useOptionPricing'

const props = defineProps<{ exchange: string; product: string }>()

const exchange = ref(props.exchange)
const product = ref(props.product)
const viewMode = ref<'3d' | '2d'>('3d')
const loading = ref(false)
const error = ref('')
const chartRef = ref<HTMLElement>()
let chart: echarts.ECharts | null = null
let surfaceData: IVSurfaceResult | null = null

async function loadSurface() {
  loading.value = true
  error.value = ''
  try {
    surfaceData = await getIVSurface(exchange.value, product.value)
    if (!surfaceData || surfaceData.count === 0) {
      error.value = '无 IV 数据，请先在数据中心下载期权数据'
    } else {
      render()
    }
  } catch (e: any) {
    error.value = e.response?.data?.error || '加载失败'
  } finally {
    loading.value = false
  }
}

function render() {
  if (!chart || !surfaceData) return
  const d = surfaceData

  if (viewMode.value === '3d') {
    render3D(d)
  } else {
    render2D(d)
  }
}

function render3D(d: IVSurfaceResult) {
  // echarts-gl surface
  const data: [number, number, number][] = []
  for (let i = 0; i < d.expiry_days.length; i++) {
    for (let j = 0; j < d.strikes.length; j++) {
      data.push([d.strikes[j], d.expiry_days[i], d.surface[i][j]])
    }
  }
  // 原始数据点散点
  const scatter = d.raw_points.map(p => [p.strike, p.expiry_days, p.iv] as [number, number, number])

  chart!.setOption({
    backgroundColor: 'transparent',
    tooltip: {
      formatter: (p: any) => {
        if (p.data) {
          return `K: ${p.data[0]}<br/>T: ${p.data[1]}d<br/>IV: ${(p.data[2] * 100).toFixed(1)}%`
        }
        return ''
      }
    },
    xAxis3D: {
      type: 'value', name: '行权价',
      axisLabel: { color: '#8899bb' },
    },
    yAxis3D: {
      type: 'value', name: '到期天数',
      axisLabel: { color: '#8899bb' },
    },
    zAxis3D: {
      type: 'value', name: 'IV',
      axisLabel: { color: '#8899bb' },
    },
    grid3D: {
      viewControl: { projection: 'perspective' },
      light: { main: { intensity: 1.2 }, ambient: { intensity: 0.3 } },
    },
    series: [
      {
        type: 'surface',
        data,
        shading: 'color',
        itemStyle: { opacity: 0.8 },
      },
      {
        type: 'scatter3D',
        data: scatter,
        symbolSize: 6,
        itemStyle: { color: '#ffa726' },
      }
    ],
  }, true)
}

function render2D(d: IVSurfaceResult) {
  // 热力图
  const data: [number, number, number][] = []
  for (let i = 0; i < d.expiry_days.length; i++) {
    for (let j = 0; j < d.strikes.length; j++) {
      data.push([j, i, d.surface[i][j]])
    }
  }

  const maxIV = Math.max(...d.raw_points.map(p => p.iv)) * 1.2

  chart!.setOption({
    backgroundColor: 'transparent',
    tooltip: {
      formatter: (p: any) => {
        if (p.data) {
          const j = p.data[0], i = p.data[1]
          return `K: ${d.strikes[j]}<br/>T: ${d.expiry_days[i]}d<br/>IV: ${(p.data[2] * 100).toFixed(1)}%`
        }
        return ''
      }
    },
    grid: { left: 80, right: 60, top: 30, bottom: 50 },
    xAxis: {
      type: 'category',
      data: d.strikes.map(s => s.toFixed(2)),
      name: '行权价',
      nameTextStyle: { color: '#8899bb' },
      axisLabel: { color: '#8899bb', fontSize: 10 },
      splitLine: { show: false },
    },
    yAxis: {
      type: 'category',
      data: d.expiry_days.map(e => `${e}d`),
      name: '到期天数',
      nameTextStyle: { color: '#8899bb' },
      axisLabel: { color: '#8899bb', fontSize: 10 },
    },
    visualMap: {
      min: 0, max: maxIV,
      calculable: true,
      orient: 'vertical',
      right: 0, top: 'center',
      inRange: { color: ['#1a237e', '#283593', '#1565c0', '#2e7d32', '#f9a825', '#e65100'] },
      textStyle: { color: '#8899bb' },
      formatter: (v: number) => `${(v * 100).toFixed(0)}%`,
    },
    series: [{
      type: 'heatmap',
      data,
      label: { show: false },
    }],
  }, true)
}

function handleResize() { chart?.resize() }

onMounted(() => {
  if (chartRef.value) {
    chart = echarts.init(chartRef.value)
  }
  window.addEventListener('resize', handleResize)
  loadSurface()
})

onUnmounted(() => {
  window.removeEventListener('resize', handleResize)
  chart?.dispose()
})

watch(viewMode, render)
</script>

<style scoped>
.iv-surface-chart { height: 100%; display: flex; flex-direction: column; }
.toolbar {
  display: flex; gap: 12px; align-items: center;
  padding: 8px 0; flex-shrink: 0;
}
.toolbar .form-row { display: flex; align-items: center; gap: 6px; }
.toolbar label { font-size: 12px; color: #8899bb; }
.toolbar select {
  background: rgba(26, 34, 54, 0.8);
  border: 1px solid rgba(74, 85, 104, 0.3);
  border-radius: 4px; color: #e0e0e0;
  padding: 4px 8px; font-size: 12px;
}
.btn-toggle {
  padding: 4px 12px; background: rgba(41, 98, 255, 0.2);
  border: 1px solid rgba(41, 98, 255, 0.4); border-radius: 4px;
  color: #2962ff; font-size: 12px; cursor: pointer;
}
.btn-toggle:hover { background: rgba(41, 98, 255, 0.3); }
.loading-hint, .error-hint {
  flex: 1; display: flex; align-items: center; justify-content: center;
  font-size: 13px;
}
.loading-hint { color: #8899bb; }
.error-hint { color: #ef5350; }
.chart-area { flex: 1; min-height: 400px; }
</style>
