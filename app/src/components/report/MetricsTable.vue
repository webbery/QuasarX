<!-- app/src/components/report/MetricsTable.vue -->
<!-- 策略指标表格组件 -->

<template>
  <div class="chart-card full-width">
    <div class="chart-title">
      <div class="title-icon">📋</div>
      <span>Strategy Static Table</span>
    </div>
    <div class="table-container">
      <div class="table-wrapper" ref="tableWrapperRef">
        <table>
          <thead>
            <tr>
              <th>指标</th>
              <th>数值</th>
              <th>基准</th>
              <th>对比</th>
            </tr>
          </thead>
          <tbody>
            <tr v-for="row in tableRows" :key="row.key">
              <td>{{ row.name }}</td>
              <td>{{ row.value }}</td>
              <td class="benchmark-value">{{ row.benchmark }}</td>
              <td :class="row.comparison.type">{{ row.comparison.value }}</td>
            </tr>
          </tbody>
        </table>
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, computed, onMounted } from 'vue'
import type { BenchmarkMetrics } from '@/lib/tickflow'

interface Props {
  metrics: Record<string, number>
  benchmarkMetrics: BenchmarkMetrics | null
}

const props = defineProps<Props>()
const tableWrapperRef = ref<HTMLElement | null>(null)

// === 常量定义 ===

const metricNameMap: Record<string, string> = {
  total_return: '总收益率',
  annual_return: '年化收益率',
  max_drawdown: '最大回撤',
  sharp: '夏普比率',
  calmar_ratio: '卡玛比率',
  win_rate: '胜率',
  num_trades: '交易次数',
  VAR: '在险价值 (VaR)',
  ES: '预期短缺 (ES)',
  annual_sharp: '年化夏普比率',
  information_ratio: '信息比率',
  volatility: '波动率',
  alpha: 'Alpha',
  beta: 'Beta',
  avg_holding_days: '平均持仓天数',
  profit_loss_ratio: '盈亏比',
}

const metricOrder = [
  'total_return', 'annual_return', 'max_drawdown', 'sharp', 'calmar_ratio',
  'win_rate', 'num_trades', 'VAR', 'ES', 'volatility', 'alpha', 'beta',
  'annual_sharp', 'information_ratio', 'avg_holding_days', 'profit_loss_ratio'
]

const defaultBenchmarks: Record<string, number> = {
  total_return: 0.10,
  annual_return: 0.08,
  max_drawdown: -0.20,
  sharp: 1.0,
  win_rate: 0.50,
  num_trades: 50,
  volatility: 0.20,
  alpha: 0,
  beta: 1.0,
}

// === 工具函数 ===

function formatMetricValue(key: string, value: number): string {
  const ratioKeys = ['ratio', 'rate', 'return', 'drawdown', 'volatility', 'alpha', 'beta', 'sharp']
  const isRatio = ratioKeys.some(k => key.toLowerCase().includes(k))

  if (isRatio) {
    if (Math.abs(value) < 1) {
      return `${(value * 100).toFixed(2)}%`
    }
    return value.toFixed(4)
  }

  const countKeys = ['num', 'count', 'days', 'trades']
  const isCount = countKeys.some(k => key.toLowerCase().includes(k))
  if (isCount) {
    return Math.round(value).toString()
  }

  return value.toFixed(4)
}

function getBenchmarkValue(key: string): string {
  // 优先使用动态基准
  if (props.benchmarkMetrics && (props.benchmarkMetrics as any)[key] !== undefined) {
    return formatMetricValue(key, (props.benchmarkMetrics as any)[key])
  }

  // Fallback 到默认基准
  const benchmark = defaultBenchmarks[key]
  if (benchmark !== undefined) {
    return formatMetricValue(key, benchmark)
  }
  return '-'
}

function getComparison(key: string, actualValue: number): { value: string, type: 'positive' | 'neutral' | 'negative' } {
  let benchmark: number | undefined

  // 优先使用动态基准
  if (props.benchmarkMetrics && (props.benchmarkMetrics as any)[key] !== undefined) {
    benchmark = (props.benchmarkMetrics as any)[key]
  }

  // 没有动态基准，使用默认
  if (benchmark === undefined) {
    benchmark = defaultBenchmarks[key]
  }

  if (benchmark === undefined) {
    return { value: '-', type: 'neutral' }
  }

  const diff = actualValue - benchmark
  const isPositive = diff > 0
  const isInverted = key === 'max_drawdown' || key === 'volatility'

  const ratioKeys = ['ratio', 'rate', 'return', 'drawdown', 'volatility', 'alpha', 'beta', 'sharp']
  const isRatio = ratioKeys.some(k => key.toLowerCase().includes(k))

  let displayValue: string
  if (isRatio) {
    displayValue = `${diff > 0 ? '+' : ''}${(diff * 100).toFixed(2)}%`
  } else {
    displayValue = `${diff > 0 ? '+' : ''}${diff.toFixed(2)}`
  }

  const type = isInverted ? (diff < 0 ? 'positive' : 'negative') : (isPositive ? 'positive' : 'neutral')
  return { value: displayValue, type }
}

// === 计算属性 ===

interface TableRow {
  key: string
  name: string
  value: string
  benchmark: string
  comparison: { value: string, type: 'positive' | 'neutral' | 'negative' }
}

const tableRows = computed<TableRow[]>(() => {
  const features = props.metrics
  const rows: TableRow[] = []

  // 按照定义的顺序添加行
  metricOrder.forEach(key => {
    if (features.hasOwnProperty(key)) {
      const value = features[key]
      const chineseName = metricNameMap[key] || key
      const formattedValue = formatMetricValue(key, value)
      const benchmark = getBenchmarkValue(key)
      const comparison = getComparison(key, value)

      rows.push({
        key,
        name: chineseName,
        value: formattedValue,
        benchmark,
        comparison
      })
    }
  })

  // 如果还有未排序的指标，追加到后面
  const addedKeys = new Set(metricOrder)
  Object.keys(features).forEach(key => {
    if (!addedKeys.has(key)) {
      const value = features[key]
      const chineseName = metricNameMap[key] || key
      const formattedValue = formatMetricValue(key, value)

      rows.push({
        key,
        name: chineseName,
        value: formattedValue,
        benchmark: '-',
        comparison: { value: '-', type: 'neutral' }
      })
    }
  })

  return rows
})

// === 交互逻辑 ===

onMounted(() => {
  if (tableWrapperRef.value) {
    initDragScroll()
  }
})

function initDragScroll() {
  const tableWrapper = tableWrapperRef.value
  if (!tableWrapper) return

  let isDragging = false
  let startX = 0
  let scrollLeft = 0

  tableWrapper.addEventListener('mousedown', (e: MouseEvent) => {
    isDragging = true
    startX = e.pageX - tableWrapper.offsetLeft
    scrollLeft = tableWrapper.scrollLeft
    tableWrapper.style.cursor = 'grabbing'
  })

  tableWrapper.addEventListener('mousemove', (e: MouseEvent) => {
    if (!isDragging) return
    e.preventDefault()
    const x = e.pageX - tableWrapper.offsetLeft
    const walk = (x - startX) * 2
    tableWrapper.scrollLeft = scrollLeft - walk
  })

  tableWrapper.addEventListener('mouseup', () => {
    isDragging = false
    tableWrapper.style.cursor = 'grab'
  })

  tableWrapper.addEventListener('mouseleave', () => {
    isDragging = false
    tableWrapper.style.cursor = 'grab'
  })
}
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

.table-container {
  display: flex;
  flex-direction: column;
  gap: 15px;
}

.table-wrapper {
  overflow-x: auto;
  overflow-y: hidden;
  border-radius: 8px;
  border: 1px solid var(--border, #2a3449);
  cursor: grab;
  transition: transform 0.3s ease;
}

.table-wrapper:active {
  cursor: grabbing;
}

.table-wrapper::-webkit-scrollbar {
  height: 8px;
}

.table-wrapper::-webkit-scrollbar-track {
  background: rgba(42, 52, 77, 0.3);
  border-radius: 4px;
}

.table-wrapper::-webkit-scrollbar-thumb {
  background: rgba(41, 98, 255, 0.5);
  border-radius: 4px;
}

.table-wrapper::-webkit-scrollbar-thumb:hover {
  background: rgba(41, 98, 255, 0.7);
}

table {
  width: 100%;
  border-collapse: collapse;
  margin: 0;
  min-width: 800px;
}

th, td {
  padding: 14px 16px;
  text-align: left;
  border-bottom: 1px solid var(--border, #2a3449);
  white-space: nowrap;
}

th {
  background-color: rgba(42, 52, 77, 0.5);
  font-weight: 600;
  color: var(--text, #e0e0e0);
  font-size: 14px;
  position: sticky;
  top: 0;
  z-index: 10;
}

td {
  color: var(--text, #e0e0e0);
  font-size: 14px;
}

tbody tr:hover {
  background-color: rgba(42, 52, 77, 0.3);
}

.positive {
  color: #00c853;
  font-weight: 600;
}

.neutral {
  color: #ff6d00;
  font-weight: 600;
}

.negative {
  color: #ff1744;
  font-weight: 600;
}

.benchmark-value {
  color: #ff9800;
  font-weight: 500;
}

@media (max-width: 768px) {
  .chart-card {
    padding: 16px;
  }
}
</style>
