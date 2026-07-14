<template>
  <div class="fundamental-heatmap" ref="chartRef" style="width: 100%; height: 100%"></div>
</template>

<script setup lang="ts">
import { watch, onMounted, computed } from 'vue'
import { useECharts, createBaseChartOption } from '../../report/composables/useECharts'
import { ALL_HEATMAP_METRICS } from '../composables/useFundamentalState'
import type { FinanceRow } from '../composables/useFundamentalState'

const props = defineProps<{
  profitData: FinanceRow[]
  growthData: FinanceRow[]
  balanceData: FinanceRow[]
  cashflowData: FinanceRow[]
  metrics: string[]  // 选中的指标 key 列表
}>()

const { chartRef, initChart, updateChart } = useECharts(false)
onMounted(() => initChart())

// 合并所有季度数据，按 stat_date 去重
const allQuarters = computed(() => {
  const map = new Map<string, FinanceRow>()
  for (const row of props.profitData) map.set(row.stat_date, { ...map.get(row.stat_date), ...row })
  for (const row of props.growthData) map.set(row.stat_date, { ...map.get(row.stat_date), ...row })
  for (const row of props.balanceData) map.set(row.stat_date, { ...map.get(row.stat_date), ...row })
  for (const row of props.cashflowData) map.set(row.stat_date, { ...map.get(row.stat_date), ...row })
  return [...map.entries()].sort((a, b) => a[0].localeCompare(b[0]))
})

watch(() => [props.profitData, props.metrics] as const, () => {
  const quarters = allQuarters.value
  if (quarters.length === 0 || props.metrics.length === 0) return

  const dates = quarters.map(([d]) => d)
  const metricLabels = props.metrics.map(key => {
    const def = ALL_HEATMAP_METRICS.find(m => m.key === key)
    return def?.label || key
  })

  // 构建热力图数据: [x(季度index), y(指标index), value]
  const heatData: [number, number, number][] = []
  for (let xi = 0; xi < dates.length; xi++) {
    const row = quarters[xi][1]
    for (let yi = 0; yi < props.metrics.length; yi++) {
      const val = row[props.metrics[yi]]
      if (val != null && !isNaN(val)) {
        heatData.push([xi, yi, val])
      }
    }
  }

  // 计算 min/max 用于颜色映射
  const values = heatData.map(d => d[2])
  const minVal = Math.min(...values)
  const maxVal = Math.max(...values)

  const option = createBaseChartOption({
    title: { text: '基本面热力图', left: 'center', textStyle: { color: '#e0e0e0', fontSize: 13 } },
    tooltip: {
      formatter: (params: any) => {
        const [xi, yi, val] = params.data
        return `${dates[xi]}<br/>${metricLabels[yi]}: ${formatValue(val, props.metrics[yi])}`
      },
    },
    grid: { left: 90, right: 40, top: 40, bottom: 60 },
    xAxis: {
      type: 'category',
      data: dates,
      axisLabel: { color: '#a0aec0', fontSize: 10, rotate: 45 },
    },
    yAxis: {
      type: 'category',
      data: metricLabels,
      axisLabel: { color: '#a0aec0', fontSize: 11 },
    },
    visualMap: {
      min: minVal,
      max: maxVal,
      calculable: true,
      orient: 'horizontal',
      left: 'center',
      bottom: 5,
      inRange: { color: ['#ff3d00', '#333', '#00c853'] },
      textStyle: { color: '#a0aec0' },
    },
    series: [{
      type: 'heatmap',
      data: heatData,
      label: { show: heatData.length < 50, fontSize: 9, color: '#e0e0e0',
        formatter: (p: any) => formatValue(p.data[2], props.metrics[p.data[1]]),
      },
      itemStyle: { borderColor: '#1a1a2e', borderWidth: 1 },
    }],
  })

  updateChart(option)
}, { immediate: true, deep: true })

function formatValue(val: number, key: string): string {
  // 比率类指标显示为百分比
  const ratioKeys = ['roe_avg', 'np_margin', 'gp_margin', 'yoy_equity', 'yoy_asset', 'yoy_ni', 'yoy_eps_basic', 'debt_to_asset']
  if (ratioKeys.includes(key)) return (val * 100).toFixed(1) + '%'
  if (key === 'eps_ttm') return val.toFixed(2)
  return val.toFixed(0)
}
</script>
