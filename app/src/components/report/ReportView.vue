<!-- app/src/components/report/ReportView.vue -->
<!-- 报表视图主容器 - 协调子组件和状态管理 -->

<template>
  <div class="report-container">
    <!-- 图表网格 -->
    <div class="grid-container">
      <!-- 动态渲染可见图表 -->
      <template v-for="chart in visibleCharts" :key="chart.id">
        <div class="chart-row" :class="{ 'full-width': chart.span === 'full' }">
          <!-- PriceTrendChart -->
          <PriceTrendChart
            v-if="chart.id === 'priceTrend'"
            :prices="dataState.symbolPrices.value"
            :buy-signals="dataState.buySignals.value"
            :sell-signals="dataState.sellSignals.value"
            :symbols="selectedSymbol"
            :selected-symbol="selectedSymbol[0]"
            @symbol-change="onSymbolChange"
          />

          <!-- PerformanceChart (合并后的策略绩效 + 基准对比) -->
          <PerformanceChart
            v-else-if="chart.id === 'performance'"
            :dates="strategyPerformanceDates"
            :strategy-returns="strategyPerformanceData"
            :benchmark-data="benchmarkData"
            :benchmark-name="benchmarkName"
            :selected-benchmark="selectedBenchmark"
            @benchmark-change="onBenchmarkChange"
            @refresh-benchmark="refreshBenchmark"
          />

          <!-- SkewnessChart (偏度与峰度分析) -->
          <SkewnessChart
            v-else-if="chart.id === 'skewness'"
            :skewness="metricsData.skewness || 0.35"
            :kurtosis="metricsData.kurtosis || 3.12"
            :prices="dataState.symbolPrices.value"
          />

          <!-- 其他图表占位（后续迁移时添加） -->
          <div v-else class="chart-card placeholder">
            <div class="placeholder-content">
              <div class="placeholder-icon">🚧</div>
              <div class="placeholder-text">{{ chart.label }} 迁移中...</div>
            </div>
          </div>
        </div>
      </template>

      <!-- 原有图表的兼容处理：如果不在新注册表中，仍然显示 -->
      <template v-if="hasLegacyCharts">
        <LegacyCharts ref="legacyChartsRef" />
      </template>

      <!-- 指标表格 - 移入 grid-container 内部，确保正确的文档流 -->
      <div class="chart-row full-width" v-if="showMetricsTable">
        <MetricsTable
          :metrics="metricsData"
          :benchmark-metrics="benchmarkMetrics"
        />
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, computed, onMounted, nextTick, watch } from 'vue'
import { useReportState, type UseReportStateReturn } from './composables/useReportState'
import { useChartData, type UseChartDataReturn } from './composables/useChartData'
import MetricsTable from './MetricsTable.vue'

// 导入图表组件（已迁移的）
import PriceTrendChart from './charts/PriceTrendChart.vue'
import PerformanceChart from './charts/PerformanceChart.vue'
import SkewnessChart from './charts/SkewnessChart.vue'
import LegacyCharts from './LegacyCharts.vue'

// === 状态管理 ===
const reportState = useReportState()
const dataState = useChartData(reportState)

// === 解构状态（方便模板使用） ===
const {
  selectedSymbol,
  selectedBenchmark,
  metricsData,
  strategyPerformanceDates,
  strategyPerformanceData,
  benchmarkData,
  benchmarkName,
  chartVisibility,
  showMetricsTable,
  visibleCharts,
} = reportState

const {
  symbolPrices,
  buySignals,
  sellSignals,
  benchmarkMetrics,
  updatePrice,
  updateTradeSignals,
  updateMetrics,
  updateBenchmark,
  loadBenchmark,
  refreshBenchmark,
  loadBacktestResultFromVersion
} = dataState

// === 本地状态 ===
const legacyChartsRef = ref<any>(null)

// === 计算属性 ===
const hasLegacyCharts = computed(() => {
  // 检查是否有旧图表需要渲染
  return false // 暂时返回 false，后续迁移完成后删除
})

// === 事件处理 ===
function onSymbolChange(newSymbol: string) {
  if (newSymbol && selectedSymbol.value.length > 0) {
    selectedSymbol.value[0] = newSymbol
    // 触发价格更新
    if (dataState.symbolPrices.value.length > 0) {
      const firstDate = dataState.symbolPrices.value[0][0]
      const lastDate = dataState.symbolPrices.value[dataState.symbolPrices.value.length - 1][0]
      updatePrice(newSymbol, firstDate, lastDate)
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
  
  // 延迟初始化图表（等待 DOM 渲染完成）
  nextTick(() => {
    initializeCharts()
  })
})

function initializeCharts() {
  console.info('[ReportView] 开始初始化图表')
  // 注意：具体图表的初始化由各自的组件处理
  // 这里只处理旧图表的兼容初始化（如果有）
  if (legacyChartsRef.value) {
    legacyChartsRef.value?.initializeCharts()
  }
}

// === 暴露 API（保持向后兼容） ===
defineExpose({
  // 原有 API
  updatePrice,
  updatePriceChart: () => {
    // 委托给子组件处理
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

.grid-container {
  display: flex;
  flex-direction: column;
  flex: 1;
  gap: 20px;
  width: 100%;
  padding: 20px;
  min-height: 0;
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

/* 响应式设计 */
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

  .config-trigger {
    width: 40px;
    height: 40px;
    font-size: 20px;
  }
}
</style>
