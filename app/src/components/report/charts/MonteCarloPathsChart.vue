<!-- app/src/components/report/charts/MonteCarloPathsChart.vue -->
<!-- 蒙特卡洛模拟路径可视化 -->

<template>
  <div class="chart-card full-width">
    <div class="chart-title">
      <div class="title-icon">🎲</div>
      <span>Monte Carlo Paths</span>
      <div class="chart-controls">
        <select v-model="activeTab" @change="onTabChange" class="tab-select">
          <option value="worst">🔴 最差路径 (失效场景)</option>
          <option value="best">🟢 最好路径 (有效场景)</option>
          <option value="benchmark">⚪ 基准路径 (P10/P50/P90)</option>
        </select>
      </div>
    </div>

    <div class="chart-container" ref="chartRef"></div>
  </div>
</template>

<script setup lang="ts">
import { ref, computed, watch, onMounted } from 'vue'
import * as echarts from 'echarts'
import { useECharts } from '../composables/useECharts'
import { buildMonteCarloOption } from '@/lib/charts/options/monteCarlo'
import type { McPathDetail } from '@/stores/history'

interface Props {
  worstPaths: McPathDetail[]
  bestPaths: McPathDetail[]
  medianPath: McPathDetail | null
  p10Path: McPathDetail | null
  p90Path: McPathDetail | null
  barCount?: number  // bar 数量，用于 x 轴标签
}

const props = defineProps<Props>()

const activeTab = ref<'worst' | 'best' | 'benchmark'>('worst')
const { chartRef, initChart, updateChart } = useECharts(true)

const currentPaths = computed(() => {
  return activeTab.value === 'worst' ? props.worstPaths : props.bestPaths
})

const pathCount = computed(() => {
  return currentPaths.value.length
})

/**
 * 构建 ECharts 配置 —— 调共享 builder（Agent 的 mc_paths 视图同款）。
 */
function buildChartOption() {
  return buildMonteCarloOption({
    mode: activeTab.value,
    worstPaths: props.worstPaths,
    bestPaths: props.bestPaths,
    medianPath: props.medianPath,
    p10Path: props.p10Path,
    p90Path: props.p90Path,
    barCount: props.barCount,
  })
}

function onTabChange() {
  updateChart(buildChartOption(), true)
}

// 监听数据变化，更新图表
watch(
  [() => props.worstPaths, () => props.bestPaths, () => props.medianPath, () => props.p10Path, () => props.p90Path, () => props.barCount],
  () => {
    // 确保图表已初始化后再更新
    if (chartRef.value) {
      updateChart(buildChartOption(), true)
    }
  },
  { deep: true, immediate: true }
)

onMounted(() => {
  console.info('[MonteCarloPathsChart] 组件已挂载', {
    worstPaths: props.worstPaths?.length || 0,
    bestPaths: props.bestPaths?.length || 0,
    hasMedian: !!props.medianPath,
    barCount: props.barCount
  })
  // 初始化 ECharts 实例
  initChart()
  updateChart(buildChartOption(), true)
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

.tab-select {
  padding: 5px 10px;
  background: rgba(0, 0, 0, 0.3);
  border: 1px solid var(--border, rgba(74, 158, 255, 0.3));
  border-radius: 6px;
  color: var(--text, #e2e8f0);
  font-size: 13px;
  outline: none;
  cursor: pointer;
  transition: all 0.2s ease;
  min-width: 220px;
  appearance: none;
  -webkit-appearance: none;
  background-image: url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' width='12' height='12' viewBox='0 0 12 12'%3E%3Cpath fill='%23a0aec0' d='M6 8L1 3h10z'/%3E%3C/svg%3E");
  background-repeat: no-repeat;
  background-position: right 8px center;
  padding-right: 28px;
}

.tab-select:hover:not(:focus) {
  border-color: rgba(41, 98, 255, 0.5);
}

.tab-select:focus {
  outline: none;
  border-color: var(--primary, #2962ff);
  box-shadow: 0 0 0 2px rgba(41, 98, 255, 0.2);
  background: rgba(0, 0, 0, 0.4);
}

.tab-select option {
  background: var(--panel-bg, #1a2236);
  color: var(--text, #e2e8f0);
  padding: 8px;
}

.path-count {
  font-size: 12px;
  color: #ff9800;
  padding: 2px 8px;
  background: rgba(255, 152, 0, 0.1);
  border-radius: 4px;
}

.chart-container {
  height: 400px;
  min-height: 400px;
  width: 100%;
  flex-shrink: 0;
}

@media (max-width: 768px) {
  .chart-card {
    padding: 16px;
  }

  .chart-container {
    height: 300px;
  }

  .chart-controls {
    flex-direction: column;
    align-items: flex-end;
    gap: 8px;
  }

  .tab-select {
    min-width: 150px;
    font-size: 12px;
    padding: 4px 8px;
  }
}
</style>
