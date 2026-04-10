<!-- app/src/components/report/charts/PriceTrendChart.vue -->
<!-- 价格趋势与买卖信号图 -->

<template>
  <div class="chart-card full-width">
    <div class="chart-title">
      <div class="title-icon">💹</div>
      <span>Price Trend & Trading Signals</span>
      <div class="chart-controls">
        <select v-model="localSymbol" @change="handleSymbolChange">
          <option v-for="symbol in symbols" :key="symbol" :value="symbol">{{ symbol }}</option>
        </select>
      </div>
    </div>
    <div class="chart-container" ref="chartRef"></div>
  </div>
</template>

<script setup lang="ts">
import { ref, watch, onMounted } from 'vue'
import * as echarts from 'echarts'
import { useECharts } from '../composables/useECharts'

interface Props {
  prices: any[]
  buySignals: any[]
  sellSignals: any[]
  symbols: string[]
  selectedSymbol: string
}

interface Emits {
  (e: 'symbolChange', symbol: string): void
}

const props = defineProps<Props>()
const emit = defineEmits<Emits>()

const localSymbol = ref(props.selectedSymbol)
const { chartRef, initChart, updateChart } = useECharts(true)

function handleSymbolChange() {
  emit('symbolChange', localSymbol.value)
}

function getPriceOption(chartData: any[], sellSignals: any[], buySignals: any[]) {
  return {
    tooltip: {
      trigger: 'axis',
      axisPointer: {
        type: 'cross',
        label: { backgroundColor: '#6a7985' }
      },
      backgroundColor: 'rgba(26, 34, 54, 0.9)',
      borderColor: '#2a3449',
      textStyle: { color: '#e0e0e0' },
      formatter: function(params: any) {
        let result = `<div style="margin: 0 0 5px 0; font-weight: bold;">${params[0].axisValue}</div>`
        params.forEach((item: any) => {
          if (item.seriesName === '价格') {
            result += `<div>${item.marker} ${item.seriesName}: <span style="color: #2962ff; font-weight: bold;">${item.value[1].toFixed(2)}</span></div>`
          } else if (item.seriesName === '买入信号') {
            result += `<div style="color: #00c853;">${item.marker} ${item.seriesName}</div>`
          } else if (item.seriesName === '卖出信号') {
            result += `<div style="color: #ff6d00;">${item.marker} ${item.seriesName}</div>`
          }
        })
        return result
      }
    },
    legend: {
      data: ['价格', '买入信号', '卖出信号'],
      textStyle: { color: '#e0e0e0' },
      top: 'top',
      right: '10%'
    },
    grid: {
      left: '3%',
      right: '4%',
      bottom: '15%',
      containLabel: true
    },
    dataZoom: [
      {
        type: 'inside',
        xAxisIndex: 0,
        filterMode: 'filter',
        zoomOnMouseWheel: true,
        moveOnMouseMove: true,
        moveOnMouseWheel: false
      },
      {
        type: 'slider',
        xAxisIndex: 0,
        filterMode: 'filter',
        bottom: '3%',
        height: 20,
        borderColor: '#2a3449',
        fillerColor: 'rgba(41, 98, 255, 0.2)',
        handleStyle: { color: '#2962ff' },
        textStyle: { color: '#a0aec0' }
      }
    ],
    brush: {
      toolbox: ['lineX', 'clear'],
      xAxisIndex: 0
    },
    toolbox: {
      feature: {
        dataZoom: { yAxisIndex: false },
        restore: {},
        saveAsImage: { pixelRatio: 2 }
      },
      right: 10,
      top: 10
    },
    xAxis: {
      type: 'category',
      data: chartData.map((item: any) => item[0]),
      axisLine: { lineStyle: { color: '#6E7079' } },
      axisLabel: {
        color: '#a0aec0',
        rotate: 0
      },
      splitLine: { show: false }
    },
    yAxis: {
      type: 'value',
      scale: true,
      axisLine: { lineStyle: { color: '#6E7079' } },
      axisLabel: { color: '#a0aec0' },
      splitLine: {
        lineStyle: { color: '#2a3449', type: 'dashed' }
      }
    },
    series: [
      {
        name: '价格',
        type: 'line',
        data: chartData.map((item: any) => item[1]),
        lineStyle: { width: 2 },
        itemStyle: { color: '#2962ff' },
        smooth: true,
        showSymbol: false,
        animationDuration: 2000,
        animationEasing: 'cubicOut'
      },
      {
        name: '买入信号',
        type: 'scatter',
        data: buySignals,
        symbol: 'triangle',
        symbolSize: 16,
        itemStyle: { color: '#00c853' },
        emphasis: { scale: 1.5 }
      },
      {
        name: '卖出信号',
        type: 'scatter',
        data: sellSignals,
        symbol: 'triangle',
        symbolSize: 16,
        symbolRotate: 180,
        itemStyle: { color: '#ff6d00' },
        emphasis: { scale: 1.5 }
      }
    ]
  }
}

// 监听数据变化，更新图表
watch(
  [() => props.prices, () => props.buySignals, () => props.sellSignals],
  ([prices, buys, sells]) => {
    if (prices.length > 0) {
      updateChart(getPriceOption(prices, sells, buys), true)
    }
  },
  { deep: true }
)

// 同步 selectedSymbol
watch(() => props.selectedSymbol, (val) => {
  localSymbol.value = val
})

onMounted(() => {
  console.info('[PriceTrendChart] 组件已挂载')
})
</script>

<style scoped>
.chart-card {
  background: var(--panel-bg, #1a2236);
  border-radius: 12px;
  padding: 20px;
  border: 1px solid var(--border, #2a3449);
  transition: all 0.3s ease;
  box-shadow: 0 4px 6px rgba(0, 0, 0, 0.1);
  display: flex;
  flex-direction: column;
  min-height: 0;
}

.chart-card:hover {
  box-shadow: 0 8px 25px rgba(0, 0, 0, 0.2);
  transform: translateY(-2px);
  border-color: #2962ff;
}

.chart-card.full-width {
  grid-column: 1 / -1;
}

.chart-title {
  font-size: 16px;
  font-weight: 600;
  margin-bottom: 16px;
  color: var(--text, #e0e0e0);
  display: flex;
  align-items: center;
  gap: 12px;
  padding-bottom: 12px;
  border-bottom: 1px solid var(--border, #2a3449);
}

.title-icon {
  font-size: 20px;
  width: 32px;
  height: 32px;
  display: flex;
  align-items: center;
  justify-content: center;
  background: rgba(41, 98, 255, 0.1);
  border-radius: 8px;
}

.chart-controls {
  margin-left: auto;
  display: flex;
  align-items: center;
  gap: 10px;
}

.chart-controls select {
  background: rgba(42, 52, 77, 0.5);
  border: 1px solid var(--border, #2a3449);
  color: var(--text, #e0e0e0);
  padding: 6px 12px;
  border-radius: 6px;
  font-size: 14px;
  cursor: pointer;
  transition: all 0.2s;
  min-width: 100px;
}

.chart-controls select:hover {
  border-color: #2962ff;
  background: rgba(41, 98, 255, 0.1);
}

.chart-controls select:focus {
  outline: none;
  border-color: #2962ff;
  box-shadow: 0 0 0 2px rgba(41, 98, 255, 0.2);
}

.chart-container {
  height: 300px;
  min-height: 300px;
  width: 100%;
  flex-shrink: 0;
}

.chart-card.full-width .chart-container {
  height: 300px;
  min-height: 300px;
}

@media (max-width: 768px) {
  .chart-card {
    padding: 16px;
  }

  .chart-container {
    height: 250px;
  }

  .chart-controls {
    flex-direction: column;
    align-items: flex-end;
    gap: 8px;
  }
}
</style>
