<!-- app/src/components/report/MetricsTable.vue -->
<!-- 策略指标表格组件 - 多列分组布局 -->

<template>
  <div class="chart-card full-width">
    <div class="chart-title">
      <div class="title-icon">📋</div>
      <span>Strategy Metrics</span>
    </div>
    <div class="metrics-grid">
      <div v-for="group in metricGroups" :key="group.title" class="metric-group">
        <div class="group-title">{{ group.title }}</div>
        <table class="group-table">
          <tbody>
            <tr v-for="item in group.items" :key="item.key">
              <td>{{ item.name }}</td>
              <td :class="getValueClass(item.key, item.value)">{{ item.displayValue }}</td>
            </tr>
          </tbody>
        </table>
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { computed } from 'vue'

interface Props {
  metrics: Record<string, number>
}

const props = defineProps<Props>()

// === 指标分组与排序 ===

interface MetricGroupDef {
  title: string
  keys: string[]
}

const metricGroupsDef: MetricGroupDef[] = [
  {
    title: '收益与风险',
    keys: [
      'total_return', 'annual_return', 'max_drawdown', 'volatility',
      'sharp', 'calmar_ratio', 'win_rate', 'num_trades',
    ],
  },
  {
    title: '尾部风险',
    keys: [
      'VAR', 'ES',
      'boot_ruin_prob_50', 'boot_ruin_prob_30',
      'boot_return_p5', 'boot_return_p50', 'boot_return_p95',
      'boot_max_dd_p50', 'boot_max_dd_p95',
      'boot_median_annual_ret', 'boot_tail_1pct_avg_dd',
    ],
  },
  {
    title: 'Bootstrap 元信息',
    keys: [
      'boot_method', 'boot_block_size',
      'boot_autocorrelation', 'boot_n_simulations',
    ],
  },
  {
    title: '压力测试 (Vol × 1.5)',
    keys: [
      'boot_stress_ruin_prob_50', 'boot_stress_ruin_prob_30',
      'boot_stress_return_p5', 'boot_stress_return_p50',
      'boot_stress_max_dd_p50',
    ],
  },
]

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
  // Bootstrap 蒙特卡洛 - 正常场景
  boot_ruin_prob_50: '爆仓概率 (<50%)',
  boot_ruin_prob_30: '爆仓概率 (<30%)',
  boot_return_p5: '收益率 P5',
  boot_return_p50: '收益率中位数',
  boot_return_p95: '收益率 P95',
  boot_max_dd_p50: '最大回撤中位数',
  boot_max_dd_p95: '最大回撤 P95',
  boot_median_annual_ret: '中位数年化收益',
  boot_tail_1pct_avg_dd: '最差1%平均回撤',
  boot_method: 'Bootstrap 方法',
  boot_block_size: '块大小',
  boot_autocorrelation: '自相关系数',
  boot_n_simulations: '模拟次数',
  // Bootstrap 蒙特卡洛 - 压力测试
  boot_stress_ruin_prob_50: '压力爆仓 (<50%)',
  boot_stress_ruin_prob_30: '压力爆仓 (<30%)',
  boot_stress_return_p5: '压力收益率 P5',
  boot_stress_return_p50: '压力收益率中位数',
  boot_stress_max_dd_p50: '压力最大回撤',
}

// === 格式化 ===

function formatMetricValue(key: string, value: number): string {
  // 特殊格式化
  if (key === 'boot_method') {
    return value === 0 ? '标准' : 'Block'
  }
  if (key === 'boot_block_size' || key === 'boot_n_simulations') {
    return Math.round(value).toString()
  }
  if (key === 'boot_autocorrelation') {
    return value.toFixed(4)
  }

  const ratioKeys = ['ratio', 'rate', 'return', 'drawdown', 'volatility', 'alpha', 'beta', 'sharp', 'prob']
  const isRatio = ratioKeys.some(k => key.toLowerCase().includes(k))

  if (isRatio) {
    if (Math.abs(value) < 1) {
      return `${(value * 100).toFixed(2)}%`
    }
    return value.toFixed(4)
  }

  const countKeys = ['num', 'count', 'days', 'trades', 'size', 'simulations', 'block']
  const isCount = countKeys.some(k => key.toLowerCase().includes(k))
  if (isCount) {
    return Math.round(value).toString()
  }

  return value.toFixed(4)
}

// === 样式辅助 ===

function getValueClass(key: string, value: number): string {
  // 正向指标：越大越好（绿色）
  const positiveKeys = ['sharp', 'calmar', 'return', 'win_rate', 'alpha', 'profit', 'median_annual', 'return_p50', 'return_p95']
  const isPositive = positiveKeys.some(k => key.toLowerCase().includes(k))

  // 负向指标：越小越好（红色）
  const negativeKeys = ['drawdown', 'ruin_prob', 'stress_ruin', 'volatility', 'beta', 'tail_1pct', 'stress_return_p5', 'stress_max_dd']
  const isNegative = negativeKeys.some(k => key.toLowerCase().includes(k))

  if (isPositive && value > 0) return 'value-positive'
  if (isNegative && value > 0.05) return 'value-negative' // 回撤/爆仓概率 > 5% 标红
  return ''
}

// === 计算属性 ===

interface MetricItem {
  key: string
  name: string
  value: number
  displayValue: string
}

interface GroupedMetrics {
  title: string
  items: MetricItem[]
}

const metricGroups = computed<GroupedMetrics[]>(() => {
  return metricGroupsDef.map(group => {
    const items: MetricItem[] = []
    for (const key of group.keys) {
      if (props.metrics.hasOwnProperty(key)) {
        const value = props.metrics[key]
        items.push({
          key,
          name: metricNameMap[key] || key,
          value,
          displayValue: formatMetricValue(key, value),
        })
      }
    }
    return { title: group.title, items }
  }).filter(g => g.items.length > 0)
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
  width: 100%;
}

.chart-card:hover {
  box-shadow: 0 8px 25px rgba(0, 0, 0, 0.2);
  transform: translateY(-2px);
  border-color: #2962ff;
}

.chart-card.full-width {
  width: 100%;
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

/* === 多列网格 === */

.metrics-grid {
  display: grid;
  grid-template-columns: repeat(4, 1fr);
  gap: 16px;
}

@media (max-width: 1400px) {
  .metrics-grid {
    grid-template-columns: repeat(2, 1fr);
  }
}

@media (max-width: 1000px) {
  .metrics-grid {
    grid-template-columns: 1fr;
  }
}

/* === 分组卡片 === */

.metric-group {
  background: rgba(42, 52, 77, 0.3);
  border-radius: 8px;
  overflow: hidden;
  border: 1px solid var(--border, #2a3449);
}

.group-title {
  padding: 10px 14px;
  font-size: 13px;
  font-weight: 600;
  color: #ff9800;
  background: rgba(42, 52, 77, 0.6);
  border-bottom: 1px solid var(--border, #2a3449);
}

.group-table {
  width: 100%;
  border-collapse: collapse;
}

.group-table tbody tr {
  border-bottom: 1px solid rgba(42, 52, 77, 0.4);
}

.group-table tbody tr:last-child {
  border-bottom: none;
}

.group-table tbody tr:hover {
  background-color: rgba(42, 52, 77, 0.3);
}

.group-table td:first-child {
  padding: 10px 14px;
  color: var(--text-secondary, #a0aec0);
  font-size: 13px;
  white-space: nowrap;
}

.group-table td:last-child {
  padding: 10px 14px;
  text-align: right;
  font-weight: 500;
  font-size: 13px;
  font-variant-numeric: tabular-nums;
  color: var(--text, #e0e0e0);
}

/* === 值着色 === */

.value-positive {
  color: #00c853 !important;
  font-weight: 600;
}

.value-negative {
  color: #ff1744 !important;
  font-weight: 600;
}

@media (max-width: 768px) {
  .chart-card {
    padding: 16px;
  }
}
</style>
