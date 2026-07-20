<template>
  <div class="timeline-chart">
    <div ref="chartRef" class="chart"></div>
  </div>
</template>

<script setup lang="ts">
import { ref, onMounted, watch, nextTick } from 'vue'
import * as echarts from 'echarts'

interface TimelineEvent {
  day: number
  type: string
  symbol?: string
  drift: number
  action: string
}

interface Props {
  events: TimelineEvent[]
  totalDays: number
  dates?: string[]
}

const props = defineProps<Props>()
const chartRef = ref<HTMLElement>()
let chartInstance: echarts.ECharts | null = null

const typeIconMap: Record<string, Record<string, string>> = {
  mean_shift: { positive: '📈', negative: '📉' },
  variance_shift: { positive: '🔺', negative: '🔻' },
  corr_spike: { positive: '🔗', negative: '🔗' },
  consensus: { positive: '⚠️', negative: '⚠️' },
}

const actionLabelMap: Record<string, string> = {
  normal: '正常 (95%)',
  ewma_99: 'EWMA 99%',
  ewma_99_5: 'EWMA 99.5%',
}

function renderChart() {
  if (!chartInstance || !props.events.length) return

  // dates 含 header 导致长度为 n+1，取 slice(1) 与收益率数量对齐
  const rawDates = props.dates && props.dates.length > props.totalDays
    ? props.dates.slice(1)
    : props.dates || []

  // 格式化日期为 YYYY-MM-DD
  const formatDates = rawDates.map((d: string) => {
    if (/^\d{4}-\d{2}-\d{2}$/.test(d)) return d
    try {
      const date = new Date(d)
      if (isNaN(date.getTime())) return d
      return `${date.getFullYear()}-${String(date.getMonth() + 1).padStart(2, '0')}-${String(date.getDate()).padStart(2, '0')}`
    } catch {
      return d
    }
  })

  const events = props.events.map((e, i) => {
    // 从 drift 值的正负判断方向，如果没有 direction 字段则自动推断
    const direction = e.drift >= 0 ? 'positive' : 'negative'
    const iconMap = typeIconMap[e.type] || { positive: '📌', negative: '📌' }
    const icon = iconMap[direction] || '📌'
    const dateLabel = formatDates[e.day] || `Day ${e.day}`
    return {
      ...e,
      direction,
      icon,
      driftDirection: direction,
      label: `${actionLabelMap[e.action] || e.action}`,
      dateLabel,
    }
  })

  const option = {
    tooltip: {
      trigger: 'item',
      formatter: (params: any) => {
        const evt = events[params.dataIndex]
        const directionLabel = evt.driftDirection === 'positive' ? '正向漂移 (C+)' : '负向漂移 (C-)'
        return `<b>${evt.dateLabel}</b><br/>标的: ${evt.symbol || '-'}<br/>方向: ${directionLabel}<br/>类型: ${evt.type}<br/>漂移: ${evt.drift.toFixed(4)}<br/>VaR 调整: ${evt.label}`
      },
    },
    legend: {
      data: ['正向漂移 (C+)', '负向漂移 (C-)'],
      top: 5,
      textStyle: { color: '#999', fontSize: 11 },
    },
    grid: { left: '3%', right: '4%', bottom: '3%', containLabel: true },
    xAxis: {
      type: 'category',
      data: events.map(e => e.dateLabel),
      axisLabel: { rotate: 45, fontSize: 11 },
    },
    yAxis: {
      type: 'value',
      name: 'Drift Magnitude',
      splitLine: { lineStyle: { color: '#2a3449' } },
    },
    series: [{
      name: '变点漂移',
      type: 'bar',
      data: events.map(e => ({
        value: e.drift,
        itemStyle: {
          color: e.drift >= 0 ? '#2962ff' : '#ff1744',  // C+ 蓝色，C- 红色
        },
      })),
      label: {
        show: true,
        position: 'top',
        formatter: (p: any) => events[p.dataIndex].icon,
        fontSize: 16,
      },
    }],
  }

  chartInstance.setOption(option)
}

onMounted(async () => {
  if (chartRef.value) {
    chartInstance = echarts.init(chartRef.value)
    await nextTick()
    renderChart()
  }
})

watch(() => props.events, renderChart, { deep: true })
</script>

<style scoped>
.timeline-chart {
  padding: 16px;
  height: 250px;
}
.chart {
  width: 100%;
  height: 100%;
}
</style>
