<!-- app/src/components/report/ReportView.vue -->
<!-- 报表视图主容器 - 协调子组件和状态管理 -->

<template>
  <div class="report-container">
    <!-- 可拖拽图表列表 -->
    <div class="grid-container" ref="gridContainer">
      <div
        v-for="(item, index) in orderedItems"
        :key="item.id"
        class="chart-row-wrapper"
        :class="{
          'full-width': item.span === 'full',
          'drag-over': dragOverIndex === index,
        }"
        :data-index="index"
        draggable="false"
      >
        <!-- 拖拽手柄 -->
        <div
          class="drag-handle"
          draggable="true"
          @dragstart="onDragStart($event, index)"
          @dragend="onDragEnd"
        >⠿</div>

        <!-- 图表内容 -->
        <div class="chart-row" :class="{ 'full-width': item.span === 'full' }">
          <component
            :is="item.component"
            v-bind="item.props"
          />
        </div>
      </div>

      <!-- 插入线（拖拽时显示） -->
      <div v-if="dragOverIndex >= 0" class="insertion-line" :style="insertionLineStyle"></div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, computed, onMounted, nextTick, h, type Component, inject } from 'vue'
import { useReportState } from './composables/useReportState'
import { useChartData } from './composables/useChartData'
import MetricsTable from './MetricsTable.vue'

// 导入图表组件（已迁移的）
import PriceTrendChart from './charts/PriceTrendChart.vue'
import PerformanceChart from './charts/PerformanceChart.vue'
import SkewnessChart from './charts/SkewnessChart.vue'
import MonteCarloPathsChart from './charts/MonteCarloPathsChart.vue'
import LegacyCharts from './LegacyCharts.vue'
import { CHART_REGISTRY } from './config/chartRegistry'

// === 状态管理 ===
const reportState = useReportState()
const dataState = useChartData(reportState)

// === 从 App.vue 注入统一的 chartVisibility 状态 ===
const appChartVisibility = inject<Ref<Record<string, boolean>>>('reportChartVisibility')

// === 解构状态 ===
const {
  selectedSymbol,
  selectedBenchmark,
  metricsData,
  strategyPerformanceDates,
  strategyDailyReturns,
  benchmarkData,
  benchmarkName,
  layout,
  saveLayout,
} = reportState

// 优先使用 App.vue 提供的 chartVisibility（确保配置面板和图表视图使用同一状态）
const chartVisibility = appChartVisibility ?? reportState.chartVisibility

const {
  strategyReturnsEstimated,
  updatePrice,
  setSelectedSymbol,
  updatePriceForAll,
  updateTradeSignals,
  updateMetrics,
  updateBenchmark,
  loadBenchmark,
  refreshBenchmark,
  loadBacktestResultFromVersion
} = dataState

// === 本地状态 ===
const legacyChartsRef = ref<any>(null)
const gridContainer = ref<HTMLElement | null>(null)

// 拖拽状态
const dragFromIndex = ref<number>(-1)
const dragOverIndex = ref<number>(-1)

// === 图表组件映射 ===

type ChartComponent = Component

const chartComponentMap: Record<string, ChartComponent> = {
  priceTrend: PriceTrendChart,
  performance: PerformanceChart,
  skewness: SkewnessChart,
  metricsTable: MetricsTable,
  monteCarloPaths: MonteCarloPathsChart,
}

interface OrderedItem {
  id: string
  span: 'full' | 'half'
  component: ChartComponent
  props: Record<string, any>
}

const orderedItems = computed<OrderedItem[]>(() => {
  const items: OrderedItem[] = []

  // 按 layout 顺序遍历
  for (const item of layout.value) {
    // 统一检查 chartVisibility（所有图表使用同一套可见性控制）
    const isVisible = chartVisibility.value[item.id]
    if (!isVisible) continue

    const def = CHART_REGISTRY.find(c => c.id === item.id)
    if (!def) continue

    const comp = chartComponentMap[item.id]
    if (!comp) continue

    // 根据 id 构建 props
    let props: Record<string, any> = {}
    if (item.id === 'metricsTable') {
      props = { metrics: metricsData.value }
    } else if (item.id === 'priceTrend') {
      props = {
        prices: dataState.symbolPrices.value,
        'buy-signals': dataState.buySignals.value,
        'sell-signals': dataState.sellSignals.value,
        'raw-buy-signals': dataState.rawBuySignals.value,
        'raw-sell-signals': dataState.rawSellSignals.value,
        'selected-symbol': selectedSymbol.value[0] || '',
        onSymbolChange: onSymbolChange,
      }
    } else if (item.id === 'performance') {
      props = {
        dates: strategyPerformanceDates.value,
        'daily-returns': strategyDailyReturns.value,
        'benchmark-data': benchmarkData.value,
        'benchmark-name': benchmarkName.value,
        'selected-benchmark': selectedBenchmark.value,
        'is-estimated': strategyReturnsEstimated.value,
        onBenchmarkChange: onBenchmarkChange,
        onRefreshBenchmark: refreshBenchmark,
      }
    } else if (item.id === 'skewness') {
      props = {
        skewness: metricsData.value.skewness || 0.35,
        kurtosis: metricsData.value.kurtosis || 3.12,
        prices: getActiveSymbolPrices.value,
        'symbol-name': activeSymbolName.value,
      }
    } else if (item.id === 'monteCarloPaths') {
      props = {
        worstPaths: dataState.mcPaths.value?.worst || [],
        bestPaths: dataState.mcPaths.value?.best || [],
        medianPath: dataState.mcPaths.value?.median || null,
        p10Path: dataState.mcPaths.value?.p10 || null,
        p90Path: dataState.mcPaths.value?.p90 || null,
        barCount: strategyDailyReturns.value.length || 252,
      }
    }

    items.push({ id: item.id, span: def.span, component: comp, props })
  }

  return items
})

// === 计算属性 ===

const getActiveSymbolPrices = computed(() => {
  const prices = dataState.symbolPrices.value
  const activeSymbol = selectedSymbol.value[0]
  return activeSymbol && prices[activeSymbol] ? prices[activeSymbol] : []
})

const activeSymbolName = computed(() => {
  return selectedSymbol.value[0] || ''
})

// 插入线位置
const insertionLineStyle = computed(() => {
  if (dragOverIndex.value < 0) return {}
  const wrappers = gridContainer.value?.querySelectorAll('.chart-row-wrapper')
  if (!wrappers || !wrappers.length) return { top: '0px' }

  if (dragOverIndex.value >= wrappers.length) {
    // 拖到最后一条之后 → 插入线在最后元素底部
    const last = wrappers[wrappers.length - 1] as HTMLElement
    return { top: `${last.offsetTop + last.offsetHeight}px` }
  }

  const target = wrappers[dragOverIndex.value] as HTMLElement
  return { top: `${target.offsetTop}px` }
})

// === 拖拽事件 ===

function onDragStart(event: DragEvent, index: number) {
  dragFromIndex.value = index
  event.dataTransfer!.effectAllowed = 'move'
  event.dataTransfer!.setData('text/plain', String(index))
}

function onDragEnd() {
  // 如果拖拽到了有效位置，执行重新排序
  if (dragOverIndex.value >= 0 && dragFromIndex.value >= 0 && dragOverIndex.value !== dragFromIndex.value) {
    reorderItems(dragFromIndex.value, dragOverIndex.value)
  }
  dragFromIndex.value = -1
  dragOverIndex.value = -1
}

function reorderItems(fromIndex: number, toIndex: number) {
  const arr = [...layout.value]
  const [moved] = arr.splice(fromIndex, 1)
  arr.splice(toIndex, 0, moved)
  layout.value = arr
  saveLayout()
}

// === 事件处理 ===

function onSymbolChange(newSymbol: string) {
  if (newSymbol && selectedSymbol.value.length > 0) {
    selectedSymbol.value[0] = newSymbol
    const prices = dataState.symbolPrices.value
    if (!prices[newSymbol]) {
      const firstKey = Object.keys(prices)[0]
      if (firstKey && prices[firstKey].length > 0) {
        const firstDate = prices[firstKey][0][0]
        const lastDate = prices[firstKey][prices[firstKey].length - 1][0]
        updatePrice(newSymbol, firstDate, lastDate)
      }
    }
  }
}

function onBenchmarkChange() {
  localStorage.setItem('benchmark_symbol', selectedBenchmark.value)
  if (reportState.backtestStartDate.value && reportState.backtestEndDate.value) {
    loadBenchmark(reportState.backtestStartDate.value, reportState.backtestEndDate.value)
  }
}

// === 初始化 ===

onMounted(() => {
  console.info('[ReportView] 组件已挂载')

  // 设置容器 dragover 事件
  const container = gridContainer.value
  if (container) {
    container.addEventListener('dragover', onContainerDragOver)
    container.addEventListener('dragleave', onContainerDragLeave)
    container.addEventListener('drop', onContainerDrop)
  }

  nextTick(() => {
    initializeCharts()
  })
})

function onContainerDragOver(event: DragEvent) {
  event.preventDefault()
  event.dataTransfer!.dropEffect = 'move'

  // 查找鼠标下方的图表 wrapper
  const wrapper = (event.target as HTMLElement).closest('.chart-row-wrapper')
  if (wrapper) {
    const index = parseInt((wrapper as HTMLElement).dataset.index || '-1', 10)
    dragOverIndex.value = index
  }
}

function onContainerDragLeave() {
  dragOverIndex.value = -1
}

function onContainerDrop(event: DragEvent) {
  event.preventDefault()
  // 实际排序在 onDragEnd 中处理
}

function initializeCharts() {
  console.info('[ReportView] 开始初始化图表')
  if (legacyChartsRef.value) {
    legacyChartsRef.value?.initializeCharts()
  }
}

// === 暴露 API ===
defineExpose({
  updatePrice,
  setSelectedSymbol,
  updatePriceForAll,
  updatePriceChart: () => {
    console.info('[ReportView] updatePriceChart 已调用')
  },
  updateTradeSignals,
  updateMetrics,
  updateBenchmark,
  loadBacktestResultFromVersion,
  resetTableZoom: () => {
    console.info('[ReportView] resetTableZoom 已调用')
  },
  initializeCharts
})
</script>

<style scoped>
.report-container {
  position: relative;
  flex: 1;
  display: flex;
  flex-direction: column;
  min-height: 0;
  overflow-y: auto;
  scrollbar-width: thin;
  scrollbar-color: var(--primary, #2962ff) transparent;
}

.report-container::-webkit-scrollbar {
  width: 8px;
}

.report-container::-webkit-scrollbar-track {
  background: transparent;
}

.report-container::-webkit-scrollbar-thumb {
  background: var(--primary, #2962ff);
  border-radius: 4px;
}

.report-container::-webkit-scrollbar-thumb:hover {
  background: var(--primary-dark, #1e4fd9);
}

/* === 图表网格 === */

.grid-container {
  display: flex;
  flex-direction: column;
  flex: 1;
  gap: 20px;
  width: 100%;
  padding: 20px;
  min-height: 0;
  position: relative;
}

.chart-row-wrapper {
  position: relative;
  border: 2px solid transparent;
  border-radius: 14px;
  transition: border-color 0.15s ease, background-color 0.15s ease;
}

.chart-row-wrapper:hover {
  border-color: rgba(41, 98, 255, 0.15);
}

.chart-row-wrapper.drag-over {
  border-color: #2962ff;
  background: rgba(41, 98, 255, 0.05);
}

.chart-row-wrapper.full-width {
  grid-column: 1 / -1;
}

/* === 拖拽手柄 === */

.drag-handle {
  position: absolute;
  top: 8px;
  left: 8px;
  z-index: 10;
  width: 28px;
  height: 28px;
  display: flex;
  align-items: center;
  justify-content: center;
  font-size: 18px;
  color: rgba(255, 255, 255, 0.3);
  background: rgba(26, 34, 54, 0.8);
  border-radius: 6px;
  cursor: grab;
  user-select: none;
  transition: all 0.2s ease;
}

.drag-handle:hover {
  color: #2962ff;
  background: rgba(41, 98, 255, 0.15);
}

.drag-handle:active {
  cursor: grabbing;
}

.chart-row {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 20px;
  width: 100%;
}

.chart-row.full-width {
  grid-template-columns: 1fr;
}

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

.chart-card.placeholder {
  min-height: 300px;
}

.placeholder-content {
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  height: 100%;
  color: var(--text-secondary, #a0aec0);
}

.placeholder-icon {
  font-size: 48px;
  margin-bottom: 12px;
}

.placeholder-text {
  font-size: 16px;
}

/* === 插入线 === */

.insertion-line {
  position: absolute;
  left: 20px;
  right: 20px;
  height: 3px;
  background: #2962ff;
  border-radius: 2px;
  z-index: 100;
  pointer-events: none;
  box-shadow: 0 0 8px rgba(41, 98, 255, 0.4);
}

.insertion-line::before,
.insertion-line::after {
  content: '';
  position: absolute;
  top: 50%;
  transform: translateY(-50%);
  width: 10px;
  height: 10px;
  background: #2962ff;
  border-radius: 50%;
}

.insertion-line::before { left: -5px; }
.insertion-line::after { right: -5px; }

/* === 响应式 === */

@media (max-width: 1200px) {
  .chart-row {
    grid-template-columns: 1fr;
  }
}

@media (max-width: 768px) {
  .grid-container {
    padding: 12px;
    gap: 12px;
  }

  .drag-handle {
    width: 24px;
    height: 24px;
    font-size: 16px;
  }
}
</style>
