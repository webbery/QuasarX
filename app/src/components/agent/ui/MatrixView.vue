<template>
  <div class="ui-view">
    <!-- 筛选条 -->
    <div v-if="showFilter" class="matrix-filter-bar">
      <div class="filter-group">
        <label>行筛选:</label>
        <input
          v-model="rowFilter"
          type="text"
          class="filter-input"
          placeholder="输入关键词过滤行..."
        />
      </div>
      <div class="filter-group">
        <label>列筛选:</label>
        <input
          v-model="colFilter"
          type="text"
          class="filter-input"
          placeholder="输入关键词过滤列..."
        />
      </div>
      <button v-if="rowFilter || colFilter" class="filter-clear-btn" @click="clearFilters">
        <i class="fas fa-times"></i> 清除
      </button>
    </div>

    <div ref="chartRef" class="ui-view__canvas" :style="{ height: chartHeight + 'px' }"></div>
  </div>
</template>

<script setup lang="ts">
import { computed, nextTick, onMounted, ref, watch } from 'vue'
import * as echarts from 'echarts'
import { useECharts } from '@/components/report/composables/useECharts'
import { buildMatrixOption, type MatrixData } from '@/lib/charts/options/histogram'
import type { ArtifactDensity } from '@/lib/agent/artifacts'

const props = withDefaults(
  defineProps<{
    data: Record<string, unknown>
    params?: Record<string, unknown>
    height?: number
    density?: ArtifactDensity
    showFilter?: boolean
  }>(),
  { height: 300, density: 'full', showFilter: true },
)

const { chartRef, initChart, updateChart } = useECharts(false)

// 筛选状态
const rowFilter = ref('')
const colFilter = ref('')

// 原始数据
const rawData = computed(() => props.data as unknown as MatrixData)

// 过滤后的数据
const filteredData = computed<MatrixData>(() => {
  const data = rawData.value
  if (!data?.rows || !data?.cols || !data?.values) return data

  const rowKeyword = rowFilter.value.trim().toLowerCase()
  const colKeyword = colFilter.value.trim().toLowerCase()

  // 如果没有筛选条件，返回原数据
  if (!rowKeyword && !colKeyword) return data

  // 过滤行
  const filteredRowIndices = data.rows
    .map((label, idx) => ({ label, idx }))
    .filter(item => !rowKeyword || item.label.toLowerCase().includes(rowKeyword))
    .map(item => item.idx)

  // 过滤列
  const filteredColIndices = data.cols
    .map((label, idx) => ({ label, idx }))
    .filter(item => !colKeyword || item.label.toLowerCase().includes(colKeyword))
    .map(item => item.idx)

  // 构建过滤后的数据
  const filteredRows = filteredRowIndices.map(idx => data.rows[idx])
  const filteredCols = filteredColIndices.map(idx => data.cols[idx])
  const filteredValues = filteredRowIndices.map(rowIdx =>
    filteredColIndices.map(colIdx => data.values[rowIdx][colIdx])
  )

  return {
    rows: filteredRows,
    cols: filteredCols,
    values: filteredValues,
    diverging: data.diverging,
  }
})

// 图表高度（考虑筛选条）
const chartHeight = computed(() => {
  return props.showFilter ? props.height - 40 : props.height
})

const option = computed(() =>
  buildMatrixOption(filteredData.value),
)

function clearFilters() {
  rowFilter.value = ''
  colFilter.value = ''
}

function render() {
  if (!chartRef.value) return
  if (!echarts.getInstanceByDom(chartRef.value)) {
    initChart()
  }
  updateChart(option.value, true)
}

onMounted(() => nextTick(render))

watch(
  () => [props.data, props.params],
  () => nextTick(render),
  { deep: true },
)

// 筛选条件变化时重新渲染
watch([rowFilter, colFilter], () => nextTick(render))
</script>

<style scoped>
.ui-view {
  width: 100%;
}

.ui-view__canvas {
  width: 100%;
}

.matrix-filter-bar {
  display: flex;
  align-items: center;
  gap: 12px;
  padding: 8px 12px;
  background: rgba(26, 34, 54, 0.5);
  border-bottom: 1px solid rgba(74, 85, 104, 0.3);
}

.filter-group {
  display: flex;
  align-items: center;
  gap: 6px;
}

.filter-group label {
  font-size: 12px;
  color: #999;
  white-space: nowrap;
}

.filter-input {
  padding: 4px 8px;
  background: rgba(26, 34, 54, 0.8);
  border: 1px solid rgba(74, 85, 104, 0.3);
  border-radius: 4px;
  color: #e0e0e0;
  font-size: 12px;
  outline: none;
  min-width: 150px;
}

.filter-input:focus {
  border-color: rgba(41, 98, 255, 0.5);
}

.filter-input::placeholder {
  color: #666;
}

.filter-clear-btn {
  padding: 4px 8px;
  background: transparent;
  border: 1px solid rgba(74, 85, 104, 0.3);
  border-radius: 4px;
  color: #999;
  font-size: 11px;
  cursor: pointer;
  display: flex;
  align-items: center;
  gap: 4px;
  transition: all 0.2s;
}

.filter-clear-btn:hover {
  border-color: rgba(41, 98, 255, 0.5);
  color: #e0e0e0;
}
</style>
