<template>
  <div class="correlation-chart">
    <!-- 上图：滚动平均相关性 -->
    <div class="chart-panel">
      <h4>滚动平均相关性 (60 天窗口)</h4>
      <div ref="lineChartRef" class="chart"></div>
    </div>
    <!-- 下图：相关性矩阵对比 -->
    <div class="matrix-panels" v-if="matrixBefore.length && matrixAfter.length">
      <div class="matrix-panel">
        <h4>变点前 (正常)</h4>
        <div ref="beforeHeatmapRef" class="chart" @contextmenu.prevent="onContextMenu($event, 'before')"></div>
      </div>
      <div class="matrix-panel">
        <h4>变点后 (危机)</h4>
        <div ref="afterHeatmapRef" class="chart" @contextmenu.prevent="onContextMenu($event, 'after')"></div>
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, onMounted, watch, nextTick } from 'vue'
import * as echarts from 'echarts'
import { ElMessage } from 'element-plus'

interface Props {
  rollingAvg: number[]
  matrixBefore: number[][]
  matrixAfter: number[][]
  changePoints: number[]
  dates: string[]
  symbols: string[]
}

const props = defineProps<Props>()
const lineChartRef = ref<HTMLElement>()
const beforeHeatmapRef = ref<HTMLElement>()
const afterHeatmapRef = ref<HTMLElement>()

let lineChartInstance: echarts.ECharts | null = null
let beforeHeatmapInstance: echarts.ECharts | null = null
let afterHeatmapInstance: echarts.ECharts | null = null

function renderLineChart() {
  if (!lineChartInstance || !props.rollingAvg.length) return

  const n = props.rollingAvg.length
  // dates 含 header 导致长度为 n+1，取 slice(1) 与收益率数量对齐
  const rawDates = props.dates.length > n
    ? props.dates.slice(1).slice(0, n)
    : props.dates.length === n
      ? props.dates
      : []
  
  // 格式化日期为 YYYY-MM-DD
  const xData = rawDates.map((d: string) => {
    if (/^\d{4}-\d{2}-\d{2}$/.test(d)) return d
    try {
      const date = new Date(d)
      if (isNaN(date.getTime())) return d
      return `${date.getFullYear()}-${String(date.getMonth() + 1).padStart(2, '0')}-${String(date.getDate()).padStart(2, '0')}`
    } catch {
      return d
    }
  })

  const option = {
    tooltip: { trigger: 'axis' },
    legend: {
      data: ['滚动平均相关性'],
      top: 5,
      textStyle: { color: '#999', fontSize: 11 },
    },
    grid: { left: '3%', right: '4%', bottom: '3%', containLabel: true },
    xAxis: { type: 'category', data: xData, axisLabel: { rotate: 45, fontSize: 11 } },
    yAxis: {
      type: 'value',
      min: 0,
      max: 1,
      name: '平均相关系数',
      splitLine: { lineStyle: { color: '#2a3449' } },
    },
    dataZoom: [{ type: 'inside' }, { type: 'slider', height: 20, bottom: 10 }],
    series: [{
      name: '滚动平均相关性',
      type: 'line',
      data: props.rollingAvg,
      smooth: true,
      symbol: 'none',
      lineStyle: { color: '#2962ff', width: 2 },
      areaStyle: { color: 'rgba(41, 98, 255, 0.1)' },
    }],
    visualMap: {
      show: false,
      pieces: [{ gt: 0.7, color: '#ff1744' }],
    },
  }

  lineChartInstance.setOption(option)
}

function renderHeatmap(ref: any, data: number[][], symbols: string[]) {
  if (!ref || !data.length) return

  const n = symbols.length || data.length
  const heatmapData: [number, number, number][] = []
  for (let i = 0; i < n; i++) {
    for (let j = 0; j < n; j++) {
      heatmapData.push([j, i, data[i][j]])
    }
  }

  const option = {
    tooltip: {
      position: 'top',
      formatter: (p: any) => `${symbols[p.data[1]]} ↔ ${symbols[p.data[0]]}<br/>ρ = ${p.data[2].toFixed(3)}`,
    },
    grid: { top: '10%', bottom: '15%', left: '15%', right: '10%' },
    xAxis: {
      type: 'category',
      data: symbols,
      axisLabel: { rotate: 45, fontSize: 10 },
      splitArea: { show: true },
    },
    yAxis: {
      type: 'category',
      data: symbols,
      axisLabel: { fontSize: 10 },
      splitArea: { show: true },
    },
    visualMap: {
      show: true,
      min: 0,
      max: 1,
      calculable: false,
      orient: 'horizontal',
      left: 'center',
      bottom: '0%',
      inRange: { color: ['#1a2236', '#2962ff', '#ff9800', '#ff1744'] },
      text: ['高相关', '低相关'],
      textStyle: { color: '#999', fontSize: 10 },
    },
    series: [{
      name: 'Correlation',
      type: 'heatmap',
      data: heatmapData,
      label: { show: true, formatter: (p: any) => p.data[2].toFixed(2), fontSize: 10 },
      emphasis: { itemStyle: { shadowBlur: 10, shadowColor: 'rgba(0,0,0,0.5)' } },
    }],
  }

  ref.setOption(option)
}

// 右键菜单：复制全部股票名单（CUSUM 热力图无 visualMap 过滤）
function onContextMenu(_event: MouseEvent, panel: 'before' | 'after') {
  const matrix = panel === 'before' ? props.matrixBefore : props.matrixAfter
  if (!matrix.length) {
    ElMessage.warning(`${panel === 'before' ? '变点前' : '变点后'}数据为空`)
    return
  }

  const symbols = props.symbols.map(s => {
    // 转换为 sh.xxxxxx / sz.xxxxxx 格式
    const parts = s.split('.')
    if (parts.length === 2) {
      return `${parts[1].toLowerCase()}.${parts[0]}`
    }
    return s
  })

  const text = symbols.join(',')
  navigator.clipboard.writeText(text).then(() => {
    ElMessage.success(`已复制${panel === 'before' ? '变点前' : '变点后'} ${symbols.length} 只股票: ${text}`)
  }).catch(() => {
    ElMessage.error('复制到剪贴板失败')
  })
}

onMounted(async () => {
  if (lineChartRef.value) lineChartInstance = echarts.init(lineChartRef.value)
  if (beforeHeatmapRef.value) beforeHeatmapInstance = echarts.init(beforeHeatmapRef.value)
  if (afterHeatmapRef.value) afterHeatmapInstance = echarts.init(afterHeatmapRef.value)

  await nextTick()
  renderLineChart()
  if (beforeHeatmapInstance) renderHeatmap(beforeHeatmapInstance, props.matrixBefore, props.symbols)
  if (afterHeatmapInstance) renderHeatmap(afterHeatmapInstance, props.matrixAfter, props.symbols)
})

watch(() => [props.rollingAvg, props.matrixBefore, props.matrixAfter, props.symbols], () => {
  renderLineChart()
  if (beforeHeatmapInstance) renderHeatmap(beforeHeatmapInstance, props.matrixBefore, props.symbols)
  if (afterHeatmapInstance) renderHeatmap(afterHeatmapInstance, props.matrixAfter, props.symbols)
}, { deep: true })
</script>

<style scoped>
.correlation-chart {
  display: flex;
  flex-direction: column;
  gap: 16px;
  padding: 16px;
}

.chart-panel, .matrix-panel {
  background: rgba(42, 52, 77, 0.3);
  border-radius: 8px;
  padding: 12px;
}

.chart-panel h4, .matrix-panel h4 {
  margin: 0 0 12px 8px;
  font-size: 13px;
  color: #a0aec0;
  font-weight: 600;
}

.chart {
  width: 100%;
  height: 280px;
}

.matrix-panels {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 16px;
}

@media (max-width: 1200px) {
  .matrix-panels {
    grid-template-columns: 1fr;
  }
}
</style>
