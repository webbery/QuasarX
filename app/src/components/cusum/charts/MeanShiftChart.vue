<template>
  <div class="chart-container">
    <div ref="chartRef" class="chart"></div>
  </div>
</template>

<script setup lang="ts">
import { ref, onMounted, watch, nextTick, computed } from 'vue'
import * as echarts from 'echarts'

interface CusumResult {
  symbol: string
  s_pos: number[]
  s_neg: number[]
  threshold: number
  change_points: number[]
}

interface Props {
  results: CusumResult[]
  dates: string[]
  selectedSymbol?: string
}

const props = withDefaults(defineProps<Props>(), {
  selectedSymbol: '__all__',
})
const chartRef = ref<HTMLElement>()
let chartInstance: echarts.ECharts | null = null

const symbolList = computed(() => props.results.map(r => r.symbol))

// 格式化日期为 YYYY-MM-DD
function formatDate(dateStr: string): string {
  if (!dateStr) return ''
  // 如果已经是 YYYY-MM-DD 格式，直接返回
  if (/^\d{4}-\d{2}-\d{2}$/.test(dateStr)) return dateStr
  // 尝试解析其他格式
  try {
    const d = new Date(dateStr)
    if (isNaN(d.getTime())) return dateStr
    const year = d.getFullYear()
    const month = String(d.getMonth() + 1).padStart(2, '0')
    const day = String(d.getDate()).padStart(2, '0')
    return `${year}-${month}-${day}`
  } catch {
    return dateStr
  }
}

function renderChart() {
  if (!chartInstance || !props.results.length) return

  // 根据选择过滤标的
  const filtered = props.selectedSymbol === '__all__'
    ? props.results
    : props.results.filter(r => r.symbol === props.selectedSymbol)

  if (filtered.length === 0) return

  // 取第一个结果的长度作为基准
  const n = filtered[0]?.s_pos.length || 0
  if (n === 0) return

  // dates 含 header 导致长度为 n+1，取 slice(1) 与收益率数量对齐
  const rawDates = props.dates.length > n
    ? props.dates.slice(1).slice(0, n)
    : props.dates.length === n
      ? props.dates
      : []
  
  // 格式化日期为 YYYY-MM-DD
  const xData = rawDates.length > 0
    ? rawDates.map(d => formatDate(d))
    : Array.from({ length: n }, (_, i) => {
        // 如果没有日期数据，生成占位日期（从今天往前推）
        const d = new Date()
        d.setDate(d.getDate() - (n - 1 - i))
        return formatDate(d.toISOString().slice(0, 10))
      })

  // 构建 series
  const series: any[] = []
  const legendData: string[] = []

  for (const res of filtered) {
    if (res.s_pos.length !== n) continue

    const lineStyle = filtered.length === 1
      ? undefined
      : { color: '#ffffff', width: 3, type: 'solid' as const }

    // S+
    const sPosName = `${res.symbol} - S+ (正向累积偏差)`
    legendData.push(sPosName)
    series.push({
      name: sPosName,
      type: 'line',
      data: res.s_pos,
      smooth: true,
      symbol: 'none',
      lineStyle: lineStyle || { color: '#ef232a', width: 1.5 },
      tooltip: {
        formatter: (p: any) => {
          const val = p.data.toFixed(4)
          return `<b>${res.symbol}</b><br/>S+ (正向累积偏差): ${val}<br/>日期: ${xData[p.dataIndex]}`
        },
      },
    })

    // S-
    const sNegName = `${res.symbol} - S- (负向累积偏差)`
    legendData.push(sNegName)
    series.push({
      name: sNegName,
      type: 'line',
      data: res.s_neg,
      smooth: true,
      symbol: 'none',
      lineStyle: lineStyle || { color: '#2962ff', width: 1.5 },
      tooltip: {
        formatter: (p: any) => {
          const val = p.data.toFixed(4)
          return `<b>${res.symbol}</b><br/>S- (负向累积偏差): ${val}<br/>日期: ${xData[p.dataIndex]}`
        },
      },
    })

    // Threshold（单选时显示）
    if (filtered.length === 1) {
      legendData.push('Threshold (控制限)')
      series.push({
        name: 'Threshold (控制限)',
        type: 'line',
        data: Array(n).fill(res.threshold),
        symbol: 'none',
        lineStyle: { color: '#ff9800', width: 2, type: 'dashed' },
        tooltip: {
          formatter: () => `控制限阈值: ${res.threshold.toFixed(2)}`,
        },
      })
    }

    // Change Points（单选时显示）
    if (filtered.length === 1 && res.change_points.length > 0) {
      legendData.push('Change Points (变点)')
      series.push({
        name: 'Change Points (变点)',
        type: 'scatter',
        data: res.change_points.map(idx => ({
          value: [idx, res.s_pos[idx] > res.s_neg[idx] ? res.s_pos[idx] : res.s_neg[idx]],
        })),
        symbol: 'triangle',
        symbolSize: 12,
        itemStyle: { color: '#ff1744' },
        tooltip: {
          formatter: (p: any) => {
            const idx = p.data[0]
            const sVal = res.s_pos[idx] > res.s_neg[idx] ? res.s_pos[idx] : res.s_neg[idx]
            return `⚠ <b>变点 detected</b><br/>日期: ${xData[idx]}<br/>CUSUM 值: ${sVal.toFixed(4)}`
          },
        },
      })
    }
  }

  const option = {
    tooltip: {
      trigger: 'axis',
    },
    legend: {
      data: legendData,
      top: 5,
      textStyle: { color: '#999', fontSize: 10 },
      type: 'scroll',
    },
    grid: { left: '3%', right: '4%', bottom: '3%', containLabel: true },
    xAxis: {
      type: 'category',
      data: xData,
      axisLabel: { rotate: 45, fontSize: 11 },
    },
    yAxis: {
      type: 'value',
      name: 'CUSUM Statistic',
      splitLine: { lineStyle: { color: '#2a3449' } },
    },
    dataZoom: [{ type: 'inside' }, { type: 'slider', height: 20, bottom: 10 }],
    series,
  }

  chartInstance.setOption(option, true)
}

onMounted(async () => {
  if (chartRef.value) {
    chartInstance = echarts.init(chartRef.value)
    await nextTick()
    renderChart()
  }
})

watch(() => props.results, (val) => {
  // 使用 nextTick + requestAnimationFrame 避免 DOM 更新竞态
  nextTick(() => {
    requestAnimationFrame(() => {
      renderChart()
    })
  })
}, { deep: true })
watch(() => props.selectedSymbol, () => {
  requestAnimationFrame(() => {
    renderChart()
  })
})

window.addEventListener('resize', () => chartInstance?.resize())
</script>

<style scoped>
.chart-container {
  padding: 16px;
  min-height: 400px;
  height: 400px;
  display: flex;
  flex-direction: column;
}
.chart {
  flex: 1;
  width: 100%;
}
</style>
