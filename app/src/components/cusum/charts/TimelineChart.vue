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
  drift: number
  action: string
}

interface Props {
  events: TimelineEvent[]
  totalDays: number
}

const props = defineProps<Props>()
const chartRef = ref<HTMLElement>()
let chartInstance: echarts.ECharts | null = null

const typeIconMap: Record<string, string> = {
  mean_shift: '📊',
  variance_shift: '📈',
  corr_spike: '🔗',
  consensus: '⚠️',
}

const actionLabelMap: Record<string, string> = {
  normal: '正常 (95%)',
  ewma_99: 'EWMA 99%',
  ewma_99_5: 'EWMA 99.5%',
}

function renderChart() {
  if (!chartInstance || !props.events.length) return

  const events = props.events.map((e, i) => ({
    ...e,
    icon: typeIconMap[e.type] || '📌',
    label: `${actionLabelMap[e.action] || e.action}`,
  }))

  const option = {
    tooltip: {
      trigger: 'item',
      formatter: (params: any) => {
        const evt = events[params.dataIndex]
        return `<b>Day ${evt.day}</b><br/>类型: ${evt.type}<br/>漂移: ${evt.drift.toFixed(4)}<br/>VaR 调整: ${evt.label}`
      },
    },
    grid: { left: '3%', right: '4%', bottom: '3%', containLabel: true },
    xAxis: {
      type: 'category',
      data: events.map(e => `Day ${e.day}`),
      axisLabel: { rotate: 45, fontSize: 11 },
    },
    yAxis: {
      type: 'value',
      name: 'Drift Magnitude',
      splitLine: { lineStyle: { color: '#2a3449' } },
    },
    series: [{
      name: 'Change Points',
      type: 'bar',
      data: events.map(e => ({
        value: e.drift,
        itemStyle: {
          color: e.type === 'consensus' ? '#ff1744' :
                 e.type === 'corr_spike' ? '#ff9800' :
                 e.type === 'variance_shift' ? '#9c27b0' : '#2962ff',
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
