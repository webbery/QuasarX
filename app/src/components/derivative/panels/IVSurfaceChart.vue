<template>
  <div class="iv-surface-chart">
    <div class="toolbar">
      <div class="form-row">
        <label>交易所</label>
        <select v-model="exchange">
          <option value="CFFEX">CFFEX</option>
          <option value="SSE">SSE</option>
          <option value="SZSE">SZSE</option>
        </select>
      </div>
      <div class="form-row">
        <label>标的</label>
        <select v-model="product" @change="loadSurface">
          <option v-for="p in productOptions" :key="p.value" :value="p.value">{{ p.label }}</option>
        </select>
      </div>
      <button class="btn-toggle" @click="viewMode = viewMode === '3d' ? '2d' : '3d'">
        {{ viewMode === '3d' ? '切换 2D' : '切换 3D' }}
      </button>
      <!-- 2D 模式的轴选择 -->
      <template v-if="viewMode === '2d'">
        <div class="form-row">
          <label>X 轴</label>
          <select v-model="axisX" @change="render">
            <option value="strike">行权价</option>
            <option value="expiry">到期天数</option>
            <option value="iv">IV</option>
          </select>
        </div>
        <div class="form-row">
          <label>Y 轴</label>
          <select v-model="axisY" @change="render">
            <option value="strike">行权价</option>
            <option value="expiry">到期天数</option>
            <option value="iv" selected>IV</option>
          </select>
        </div>
      </template>
      <div v-if="filterStats" class="filter-badge" @click="showFilterDetail = !showFilterDetail">
        <span class="filter-count">{{ filterStats.filtered_count }}/{{ filterStats.total_contracts }}</span>
        <span class="filter-label">合约</span>
        <span class="filter-arrow" :class="{ expanded: showFilterDetail }">▼</span>
      </div>
    </div>
    <div v-if="isMismatch" class="mismatch-hint">
      当前标的与过滤器不一致，请切换或重新选择
    </div>
    <div v-if="showFilterDetail && filterStats" class="filter-detail">
      <div class="filter-summary">
        <span v-for="(count, layer) in filterStats.removed_by_layer" :key="layer" class="layer-stat">
          {{ layer }}: <strong>{{ count }}</strong>
        </span>
      </div>
      <div v-if="filterStats.removed_contracts.length > 0" class="removed-list">
        <div v-for="item in filterStats.removed_contracts" :key="item.contract_name" class="removed-item">
          <span class="removed-name">{{ item.contract_name }}</span>
          <span class="removed-layer">{{ item.layer }}</span>
          <span class="removed-reason">{{ item.reason }}</span>
        </div>
      </div>
    </div>
    <div v-if="loading" class="loading-hint">加载 IV 数据中...</div>
    <div v-else-if="error" class="error-hint">{{ error }}</div>
    <div v-else ref="chartRef" class="chart-area"></div>
  </div>
</template>

<script setup lang="ts">
import { ref, computed, watch, onMounted, onUnmounted, nextTick } from 'vue'
import * as echarts from 'echarts'
import 'echarts-gl'
import { getIVSurface, type IVSurfaceResult, type FilterStats } from '../composables/useOptionPricing'

const PRODUCT_MAP: Record<string, Array<{ value: string; label: string }>> = {
  CFFEX: [
    { value: 'IO', label: 'IO (沪深300)' },
    { value: 'HO', label: 'HO (上证50)' },
    { value: 'MO', label: 'MO (中证1000)' },
  ],
  SSE: [
    { value: '50ETF', label: '50ETF (510050)' },
    { value: '300ETF', label: '300ETF (510300)' },
    { value: '500ETF', label: '500ETF (510500)' },
    { value: 'STAR50ETF', label: '科创50ETF (588000)' },
  ],
  SZSE: [
    { value: '159919', label: '沪深300 (159919)' },
    { value: '159915', label: '创业板 (159915)' },
    { value: '159922', label: '中证500 (159922)' },
    { value: '159901', label: '深证100 (159901)' },
  ],
}

const props = defineProps<{
  exchange: string
  product: string
  active?: boolean  // 当前 tab 是否激活
}>()

const exchange = ref(props.exchange)
const product = ref(props.product)
const viewMode = ref<'3d' | '2d'>('2d')  // 默认 2D
const axisX = ref<'strike' | 'expiry' | 'iv'>('strike')  // 默认行权价
const axisY = ref<'strike' | 'expiry' | 'iv'>('iv')  // 默认 IV
const loading = ref(false)
const error = ref('')
const chartRef = ref<HTMLElement>()
const showFilterDetail = ref(false)
const filterStats = ref<FilterStats | null>(null)
let chart: echarts.ECharts | null = null
let surfaceData: IVSurfaceResult | null = null

const productOptions = computed(() => PRODUCT_MAP[exchange.value] || [])

const isMismatch = computed(() => {
  return props.exchange !== exchange.value || props.product !== product.value
})

watch(exchange, () => {
  const opts = PRODUCT_MAP[exchange.value]
  if (opts && opts.length > 0) {
    product.value = opts[0].value
    loadSurface()
  }
})

async function loadSurface() {
  loading.value = true
  error.value = ''
  showFilterDetail.value = false
  try {
    surfaceData = await getIVSurface(exchange.value, product.value)
    if (!surfaceData || surfaceData.count === 0) {
      error.value = '无 IV 数据，请先在数据中心下载期权数据'
      filterStats.value = null
      loading.value = false
    } else {
      filterStats.value = surfaceData.filter_stats || null
      loading.value = false
      await nextTick()
      render()
    }
  } catch (e: any) {
    error.value = e.response?.data?.error || '加载失败'
    filterStats.value = null
    loading.value = false
  }
}

function render() {
  if (!surfaceData) return
  // 确保 chart 已初始化 (延迟 init，避免 0×0 容器)
  if (!ensureChart()) return
  // 容器尺寸为 0 时跳过渲染 (v-show=false 时 display:none)
  // 避免 echarts-gl 全局副作用导致 "Dom has no width or height" 错误
  if (!chartRef.value || chartRef.value.offsetWidth === 0 || chartRef.value.offsetHeight === 0) return
  
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

  // 强制刷新尺寸，确保容器有宽高（修复 "Dom has no width or height"）
  chart!.resize()

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
      nameTextStyle: { color: '#e0e0e0', fontSize: 12 },
      axisLabel: { color: '#c0c8d8', fontSize: 11 },
      axisLine: { lineStyle: { color: 'rgba(255, 255, 255, 0.3)' } },
    },
    yAxis3D: {
      type: 'value', name: '到期天数',
      nameTextStyle: { color: '#e0e0e0', fontSize: 12 },
      axisLabel: { color: '#c0c8d8', fontSize: 11 },
      axisLine: { lineStyle: { color: 'rgba(255, 255, 255, 0.3)' } },
    },
    zAxis3D: {
      type: 'value', name: 'IV',
      nameTextStyle: { color: '#e0e0e0', fontSize: 12 },
      axisLabel: { color: '#c0c8d8', fontSize: 11 },
      axisLine: { lineStyle: { color: 'rgba(255, 255, 255, 0.3)' } },
    },
    grid3D: {
      viewControl: { projection: 'perspective' },
      light: { main: { intensity: 1.2 }, ambient: { intensity: 0.3 } },
    },
    series: [
      {
        type: 'surface',
        data,
        dataShape: [d.expiry_days.length, d.strikes.length],
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
  // 2D 散点/折线图，支持 X/Y 轴组合选择
  // 数据点：每个 (strike, expiry, call_put) 组合对应一个 IV 值
  const points = d.raw_points.map(p => ({
    strike: p.strike,
    expiry: p.expiry_days,
    iv: p.iv * 100,  // 转为百分比
    call_put: p.call_put,
    contract_name: p.contract_name,
  }))

  // 根据轴选择提取数据
  const getAxisValue = (axis: 'strike' | 'expiry' | 'iv', point: typeof points[0]) => {
    if (axis === 'strike') return point.strike
    if (axis === 'expiry') return point.expiry
    return point.iv  // iv
  }

  // 按到期日+认购/认沽分组（用于分组着色和连线）
  // key 格式: "expiry_callPut"，如 "10_认购"、"10_认沽"
  const expiryGroups = new Map<string, typeof points>()
  for (const p of points) {
    const key = `${p.expiry}_${p.call_put}`
    if (!expiryGroups.has(key)) {
      expiryGroups.set(key, [])
    }
    expiryGroups.get(key)!.push(p)
  }

  // 按到期日分组着色（不同到期日用不同颜色）
  const expiryColors: Record<number, string> = {
    // 近期到期：暖色
    10: '#ff6b6b',   // 红
    11: '#ffa726',   // 橙
    15: '#ffca28',   // 黄
    20: '#66bb6a',   // 绿
    30: '#42a5f5',   // 蓝
    60: '#ab47bc',   // 紫
    90: '#ec407a',   // 粉
  }
  
  const getColor = (expiry: number) => {
    // 找到最接近的预设颜色
    const keys = Object.keys(expiryColors).map(Number).sort((a, b) => a - b)
    for (let i = 0; i < keys.length; i++) {
      if (expiry <= keys[i]) return expiryColors[keys[i]]
    }
    return '#2962ff'  // 默认蓝
  }

  // 构建散点 series（按到期日+认购/认沽分组，每组一个 scatter series）
  const scatterSeries: any[] = []
  for (const [key, group] of expiryGroups) {
    const expiry = parseInt(key.split('_')[0])
    const callPut = key.split('_')[1] || '认购'  // 默认认购（防止 undefined）
    const color = getColor(expiry)
    const symbol = callPut === '认购' ? 'circle' : 'triangle'

    const seriesData = group.map(p => ({
      value: [getAxisValue(axisX.value, p), getAxisValue(axisY.value, p)],
      symbol,
      itemStyle: {
        color,
        opacity: callPut === '认沽' ? 0.6 : 1.0,
        borderColor: callPut === '认沽' ? color : 'transparent',
        borderWidth: callPut === '认沽' ? 1 : 0,
      },
      _raw: p,
    }))

    scatterSeries.push({
      name: `${expiry}天 ${callPut}`,
      type: 'scatter',
      data: seriesData,
      symbolSize: 10,
      emphasis: {
        itemStyle: {
          borderColor: '#1a2236',
          borderWidth: 2,
        },
      },
      z: 10,
    })
  }

  // 构建折线数据（按到期日+认购/认沽分组，每组一条线）
  // 认购：实线；认沽：虚线
  const lineSeries: any[] = []
  for (const [key, group] of expiryGroups) {
    const expiry = parseInt(key.split('_')[0])
    const callPut = key.split('_')[1] || '认购'  // 默认认购（防止 undefined）
    const color = getColor(expiry)
    // 认购：实线（solid）；认沽：虚线（dashed）
    const lineType = (callPut === '认沽') ? 'dashed' : 'solid'
    const lineWidth = (callPut === '认购') ? 1.5 : 1.0

    // 按 X 轴排序
    const sorted = [...group].sort((a, b) =>
      getAxisValue(axisX.value, a) - getAxisValue(axisX.value, b)
    )
    lineSeries.push({
      name: `${expiry}天 ${callPut}`,
      type: 'line',
      data: sorted.map(p => [getAxisValue(axisX.value, p), getAxisValue(axisY.value, p)]),
      lineStyle: {
        color,
        width: lineWidth,
        type: lineType,  // 'solid' 或 'dashed'
      },
      symbol: 'none',
      smooth: false,
      z: 1,  // 线在底层
    })
  }

  const axisLabels: Record<string, string> = {
    strike: '行权价',
    expiry: '到期天数',
    iv: 'IV (%)',
  }

  chart!.setOption({
    backgroundColor: 'transparent',
    tooltip: {
      trigger: 'item',  // 改为 item 触发（而非 axis）
      backgroundColor: 'rgba(26, 34, 54, 0.95)',
      borderColor: 'rgba(74, 85, 104, 0.3)',
      textStyle: { color: '#e0e0e0', fontSize: 12 },
      formatter: (params: any) => {
        // 从 params.data._raw 获取原始数据
        const raw = params.data?._raw
        if (!raw) return ''
        const cp = raw.call_put === '认购' ? '📈 认购' : '📉 认沽'
        return `${cp}<br/>合约: ${raw.contract_name}<br/>行权价: ${raw.strike}<br/>到期天数: ${raw.expiry}d<br/>IV: ${raw.iv.toFixed(2)}%`
      }
    },
    legend: {
      show: true,
      top: 8,
      right: 60,
      textStyle: { color: '#c0c8d8', fontSize: 11 },
      itemWidth: 12,
      itemHeight: 8,
    },
    grid: { left: 80, right: 40, top: 50, bottom: 60 },
    xAxis: {
      type: 'value',
      name: axisLabels[axisX.value],
      nameLocation: 'middle',  // 标题居中
      nameGap: 30,  // 标题与轴的距离
      nameTextStyle: { color: '#e0e0e0', fontSize: 12 },
      axisLabel: { 
        color: '#c0c8d8', 
        fontSize: 11,
        formatter: (v: number) => {
          // 最多保留4位小数，末尾为0则截断
          const s = v.toFixed(4)
          return parseFloat(s).toString()
        }
      },
      axisLine: { lineStyle: { color: 'rgba(255, 255, 255, 0.3)' } },
      splitLine: { lineStyle: { color: 'rgba(255, 255, 255, 0.05)' } },
      scale: true,  // 自动调整范围
      min: (value: any) => value.min * 0.98,  // 左右留 2% 空白
      max: (value: any) => value.max * 1.02,
    },
    yAxis: {
      type: 'value',
      name: axisLabels[axisY.value],
      nameLocation: 'middle',  // 标题居中
      nameGap: 50,  // 标题与轴的距离
      nameRotate: 90,  // 标题旋转90度（垂直排列）
      nameTextStyle: { color: '#e0e0e0', fontSize: 12 },
      axisLabel: { 
        color: '#c0c8d8', 
        fontSize: 11,
        formatter: (v: number) => {
          // 最多保留4位小数，末尾为0则截断
          const s = v.toFixed(4)
          return parseFloat(s).toString()
        }
      },
      axisLine: { lineStyle: { color: 'rgba(255, 255, 255, 0.3)' } },
      splitLine: { lineStyle: { color: 'rgba(255, 255, 255, 0.05)' } },
      scale: true,
      min: (value: any) => value.min * 0.95,  // 上下留 5% 空白
      max: (value: any) => value.max * 1.05,
    },
    // 数据缩放组件：支持鼠标滚轮缩放和拖拽平移
    dataZoom: [
      {
        type: 'inside',  // 内置于坐标系，支持鼠标滚轮和拖拽
        xAxisIndex: 0,
        filterMode: 'none',  // 不过滤数据，只显示可见区域
      },
      {
        type: 'inside',
        yAxisIndex: 0,
        filterMode: 'none',
      }
    ],
    series: [
      // 散点图：每个到期日+认购/认沽一个 series
      ...scatterSeries,
      // 折线图：每个到期日+认购/认沽一条线
      ...lineSeries
    ],
  }, true)
}

function handleResize() { chart?.resize() }

let resizeObserver: ResizeObserver | null = null

// 延迟初始化 echarts，只在 tab 首次激活且容器有尺寸时才 init
function ensureChart() {
  if (!chart && chartRef.value && chartRef.value.offsetWidth > 0 && chartRef.value.offsetHeight > 0) {
    chart = echarts.init(chartRef.value)
  }
  return chart
}

onMounted(() => {
  // 设置 ResizeObserver，当容器从隐藏变为可见时触发初始化
  if (chartRef.value) {
    resizeObserver = new ResizeObserver(() => {
      if (chartRef.value && chartRef.value.offsetWidth > 0) {
        ensureChart()
        if (chart) {
          chart.resize()
          if (surfaceData) render()
        }
      }
    })
    resizeObserver.observe(chartRef.value)
  }
  window.addEventListener('resize', handleResize)
  // 只在 tab 激活时加载数据
  if (props.active) {
    loadSurface()
  }
})

onUnmounted(() => {
  resizeObserver?.disconnect()
  window.removeEventListener('resize', handleResize)
  chart?.dispose()
})

// 监听 tab 激活状态，首次激活时加载数据
let loaded = false
watch(() => props.active, (isActive) => {
  if (isActive && !loaded) {
    loaded = true
    loadSurface()
  }
})

// 监听参数变化，重新加载
watch([exchange, product], () => {
  if (props.active) {
    loadSurface()
  }
})

watch(viewMode, render)
watch([axisX, axisY], render)  // 轴变化时重新渲染
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
.mismatch-hint {
  padding: 6px 12px;
  background: rgba(255, 193, 7, 0.1);
  border: 1px solid rgba(255, 193, 7, 0.3);
  border-radius: 4px;
  color: #ffc107;
  font-size: 12px;
  margin-bottom: 8px;
}
.filter-badge {
  display: flex; align-items: center; gap: 4px;
  padding: 4px 10px; background: rgba(76, 175, 80, 0.15);
  border: 1px solid rgba(76, 175, 80, 0.4); border-radius: 4px;
  color: #66bb6a; font-size: 12px; cursor: pointer;
  margin-left: auto;
}
.filter-badge:hover { background: rgba(76, 175, 80, 0.25); }
.filter-count { font-weight: 600; }
.filter-label { color: #8899bb; }
.filter-arrow { font-size: 10px; transition: transform 0.2s; }
.filter-arrow.expanded { transform: rotate(180deg); }
.filter-detail {
  background: rgba(26, 34, 54, 0.6);
  border: 1px solid rgba(74, 85, 104, 0.3);
  border-radius: 4px; padding: 8px 12px;
  margin-bottom: 8px; font-size: 12px;
  max-height: 200px; overflow-y: auto;
}
.filter-summary { display: flex; gap: 12px; flex-wrap: wrap; margin-bottom: 8px; }
.layer-stat { color: #8899bb; }
.layer-stat strong { color: #ef5350; }
.removed-list { border-top: 1px solid rgba(74, 85, 104, 0.3); padding-top: 8px; }
.removed-item {
  display: flex; gap: 8px; padding: 2px 0;
  font-family: monospace; font-size: 11px;
}
.removed-name { color: #e0e0e0; min-width: 120px; }
.removed-layer { color: #ffa726; min-width: 30px; }
.removed-reason { color: #8899bb; }
.loading-hint, .error-hint {
  flex: 1; display: flex; align-items: center; justify-content: center;
  font-size: 13px;
}
.loading-hint { color: #8899bb; }
.error-hint { color: #ef5350; }
.chart-area { flex: 1; min-height: 400px; }
</style>
