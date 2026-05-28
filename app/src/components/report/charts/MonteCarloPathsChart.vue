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
        <span v-if="pathCount > 0" class="path-count">{{ pathCount }} 条</span>
      </div>
    </div>

    <!-- 路径选择面板（仅 worst/best 显示） -->
    <div v-if="activeTab !== 'benchmark'" class="path-selector">
      <label class="select-all-label">
        <input type="checkbox" :checked="allSelected" @change="toggleAll" />
        全选
      </label>
      <div class="path-chips">
        <span
          v-for="(p, i) in currentPaths"
          :key="i"
          class="path-chip"
          :class="{ selected: selectedPaths.has(i) }"
          @click="togglePath(i)"
        >
          #{{ i + 1 }} {{ (p.total_return * 100).toFixed(1) }}%
        </span>
      </div>
    </div>

    <div class="chart-container" ref="chartRef"></div>
  </div>
</template>

<script setup lang="ts">
import { ref, computed, watch, onMounted } from 'vue'
import * as echarts from 'echarts'
import { useECharts, createBaseChartOption } from '../composables/useECharts'
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
const selectedPaths = ref<Set<number>>(new Set())
const { chartRef, initChart, updateChart } = useECharts(true)

// 默认最多显示 10 条路径
const MAX_VISIBLE_PATHS = 10

const currentPaths = computed(() => {
  return activeTab.value === 'worst' ? props.worstPaths : props.bestPaths
})

const pathCount = computed(() => {
  return currentPaths.value.length
})

const allSelected = computed(() => {
  if (currentPaths.value.length === 0) return false
  return selectedPaths.value.size === currentPaths.value.length
})

/**
 * 降采样 equity_curve 到 maxPoints 个点
 */
function downsampleCurve(curve: number[], maxPoints: number): number[] {
  if (curve.length <= maxPoints) return curve
  const ratio = (curve.length - 1) / (maxPoints - 1)
  const result: number[] = []
  for (let i = 0; i < maxPoints; i++) {
    const srcIdx = Math.round(i * ratio)
    result.push(curve[srcIdx])
  }
  return result
}

/**
 * 生成 x 轴标签（bar 索引）
 */
function generateXLabels(count: number): string[] {
  const labels: string[] = []
  for (let i = 0; i < count; i++) {
    labels.push(String(i))
  }
  return labels
}

function buildChartOption() {
  const maxPoints = 500  // 前端降采样到 500 点
  const curveLen = props.barCount || 252

  const xLabels = generateXLabels(Math.min(curveLen, maxPoints))

  const series: any[] = []

  if (activeTab.value === 'benchmark') {
    // 基准路径：P10/P50/P90
    const benchmarkPaths = [
      { path: props.p10Path, name: 'P10 (次差)', color: '#ff6d00', dashType: 'dotted' },
      { path: props.medianPath, name: 'P50 (中位数)', color: '#a0aec0', dashType: 'dashed' },
      { path: props.p90Path, name: 'P90 (次优)', color: '#00c853', dashType: 'dotted' },
    ]

    for (const bp of benchmarkPaths) {
      if (!bp.path) continue
      const curve = downsampleCurve(bp.path.equity_curve, maxPoints)
      const data = curve.map(v => Number(((v - 1) * 100).toFixed(2)))

      series.push({
        name: bp.name,
        type: 'line',
        data,
        smooth: true,
        lineStyle: {
          width: bp.name.includes('中位数') ? 3 : 2,
          color: bp.color,
          type: bp.dashType as 'solid' | 'dashed' | 'dotted'
        },
        symbol: 'none',
      })
    }
  } else {
    // worst/best 路径
    const color = activeTab.value === 'worst' ? '#f44336' : '#00c853'
    const displayCount = Math.min(selectedPaths.value.size || MAX_VISIBLE_PATHS, currentPaths.value.length)

    for (let i = 0; i < displayCount; i++) {
      const pathIdx = selectedPaths.value.size > 0
        ? Array.from(selectedPaths.value)[i]
        : i
      if (pathIdx >= currentPaths.value.length) continue

      const path = currentPaths.value[pathIdx]
      const curve = downsampleCurve(path.equity_curve, maxPoints)
      const data = curve.map(v => Number(((v - 1) * 100).toFixed(2)))

      const alpha = displayCount > 1 ? Math.max(0.15, 0.6 - i * 0.04) : 0.8

      series.push({
        name: `#${pathIdx + 1}`,
        type: 'line',
        data,
        smooth: true,
        lineStyle: {
          width: 1.5,
          color,
        },
        itemStyle: {
          opacity: alpha,
        },
        symbol: 'none',
        // 存储路径详情用于 tooltip
        _pathDetail: path,
      })
    }
  }

  // 添加 50% 爆仓阈值线
  series.push({
    name: '50% 阈值',
    type: 'line',
    data: Array.from({ length: xLabels.length }, () => -50),
    lineStyle: {
      width: 1,
      color: '#f44336',
      type: 'dotted',
    },
    symbol: 'none',
  })

  return createBaseChartOption({
    tooltip: {
      trigger: 'axis',
      axisPointer: { type: 'cross' },
      backgroundColor: 'rgba(26, 34, 54, 0.95)',
      borderColor: '#2a3449',
      textStyle: { color: '#e0e0e0', fontSize: 12 },
      formatter: function(params: any) {
        if (!params || params.length === 0) return ''

        let result = `<div style="margin: 0 0 6px 0; font-weight: bold; color: #2962ff;">Bar ${params[0].axisValue}</div>`

        // 如果是基准路径模式
        if (activeTab.value === 'benchmark') {
          params.forEach((item: any) => {
            const value = typeof item.value === 'number' ? item.value.toFixed(2) : item.value
            result += `<div>${item.marker} ${item.seriesName}: <span style="color: ${item.color}; font-weight: bold;">${value}%</span></div>`
          })
          return result
        }

        // worst/best 模式：显示前 5 条路径 + 详情
        const shown = params.slice(0, 5)
        shown.forEach((item: any) => {
          const value = typeof item.value === 'number' ? item.value.toFixed(2) : item.value
          const pd = item.series._pathDetail
          if (pd) {
            result += `<div style="margin-top: 4px; border-top: 1px solid #2a3449; padding-top: 4px;">`
            result += `<div>${item.marker} <b>${item.seriesName}</b>: <span style="color: ${item.color}; font-weight: bold;">${value}%</span></div>`
            result += `<div style="font-size: 11px; color: #a0aec0; margin-left: 16px;">`
            result += `收益率: ${(pd.total_return * 100).toFixed(1)}% | 回撤: ${(pd.max_drawdown * 100).toFixed(1)}% | 胜率: ${(pd.win_rate * 100).toFixed(0)}%`
            if (pd.longest_loss_streak > 0) {
              result += ` | 最长连亏: ${pd.longest_loss_streak}天`
            }
            if (pd.longest_win_streak > 0) {
              result += ` | 最长连胜: ${pd.longest_win_streak}天`
            }
            result += `</div></div>`
          } else {
            result += `<div>${item.marker} ${item.seriesName}: <span style="color: ${item.color}; font-weight: bold;">${value}%</span></div>`
          }
        })

        if (params.length > 5) {
          result += `<div style="color: #a0aec0; font-size: 11px; margin-top: 4px;">... 及 ${params.length - 5} 条路径</div>`
        }

        return result
      }
    },
    legend: {
      data: series.map((s: any) => s.name),
      textStyle: { color: '#e0e0e0', fontSize: 11 },
      top: activeTab.value === 'benchmark' ? 10 : undefined,
      show: activeTab.value === 'benchmark',
    },
    grid: {
      left: '3%',
      right: '4%',
      bottom: '3%',
      top: activeTab.value === 'benchmark' ? '15%' : '5%',
      containLabel: true
    },
    xAxis: {
      type: 'category',
      data: xLabels,
      axisLine: { lineStyle: { color: '#6E7079' } },
      axisLabel: {
        color: '#a0aec0',
        interval: Math.floor(xLabels.length / 10),
        formatter: (value: string) => `Day ${value}`
      },
      name: 'Bar 索引',
      nameTextStyle: { color: '#a0aec0' }
    },
    yAxis: {
      type: 'value',
      axisLabel: {
        formatter: '{value}%',
        color: '#a0aec0'
      },
      axisLine: { lineStyle: { color: '#6E7079' } },
      splitLine: {
        lineStyle: { color: '#2a3449', type: 'dashed' }
      }
    },
    series
  })
}

function onTabChange() {
  selectedPaths.value = new Set()
  // 默认选中前 MAX_VISIBLE_PATHS 条
  for (let i = 0; i < Math.min(MAX_VISIBLE_PATHS, currentPaths.value.length); i++) {
    selectedPaths.value.add(i)
  }
  updateChart(buildChartOption(), true)
}

function togglePath(index: number) {
  if (selectedPaths.value.has(index)) {
    selectedPaths.value.delete(index)
  } else {
    selectedPaths.value.add(index)
  }
  updateChart(buildChartOption(), true)
}

function toggleAll() {
  if (allSelected.value) {
    selectedPaths.value = new Set()
  } else {
    selectedPaths.value = new Set(currentPaths.value.map((_, i) => i))
  }
  updateChart(buildChartOption(), true)
}

// 监听数据变化，更新图表
watch(
  [() => props.worstPaths, () => props.bestPaths, () => props.medianPath, () => props.p10Path, () => props.p90Path, () => props.barCount],
  () => {
    // 重置选中状态
    selectedPaths.value = new Set()
    for (let i = 0; i < Math.min(MAX_VISIBLE_PATHS, currentPaths.value.length); i++) {
      selectedPaths.value.add(i)
    }
    updateChart(buildChartOption(), true)
  },
  { deep: true }
)

onMounted(() => {
  // 初始化默认选中
  for (let i = 0; i < Math.min(MAX_VISIBLE_PATHS, currentPaths.value.length); i++) {
    selectedPaths.value.add(i)
  }
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
  padding: 8px 12px;
  background: rgba(42, 52, 77, 0.5);
  border: 1px solid var(--border, #2a3449);
  border-radius: 6px;
  color: var(--text, #e0e0e0);
  font-size: 14px;
  cursor: pointer;
  transition: all 0.2s;
  min-width: 220px;
}

.tab-select:hover {
  border-color: #2962ff;
}

.path-count {
  font-size: 12px;
  color: #ff9800;
  padding: 2px 8px;
  background: rgba(255, 152, 0, 0.1);
  border-radius: 4px;
}

.path-selector {
  margin-bottom: 12px;
  padding: 10px;
  background: rgba(42, 52, 77, 0.3);
  border-radius: 8px;
  border: 1px solid var(--border, #2a3449);
}

.select-all-label {
  display: flex;
  align-items: center;
  gap: 8px;
  font-size: 13px;
  color: #a0aec0;
  cursor: pointer;
  margin-bottom: 8px;
}

.select-all-label input[type="checkbox"] {
  accent-color: #2962ff;
}

.path-chips {
  display: flex;
  flex-wrap: wrap;
  gap: 6px;
}

.path-chip {
  padding: 4px 10px;
  font-size: 12px;
  color: #a0aec0;
  background: rgba(42, 52, 77, 0.5);
  border: 1px solid var(--border, #2a3449);
  border-radius: 12px;
  cursor: pointer;
  transition: all 0.15s;
  user-select: none;
}

.path-chip:hover {
  border-color: #2962ff;
  color: #e0e0e0;
}

.path-chip.selected {
  background: rgba(41, 98, 255, 0.2);
  border-color: #2962ff;
  color: #2962ff;
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
