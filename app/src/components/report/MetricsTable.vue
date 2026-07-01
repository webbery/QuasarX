<!-- app/src/components/report/MetricsTable.vue -->
<!-- 策略指标表格组件 - 多列分组布局 -->

<template>
  <div class="chart-card full-width">
    <div class="chart-title">
      <div class="title-icon">📋</div>
      <span>Strategy Metrics</span>
    </div>
    <div v-if="metricGroups.length === 0" class="empty-state">
      <div class="empty-icon">📊</div>
      <div class="empty-text">暂无指标数据</div>
      <div class="empty-hint">请执行回测或加载策略版本</div>
    </div>
    <div v-else class="metrics-grid">
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
import { computed, onMounted } from 'vue'

interface Props {
  metrics: Record<string, number>
}

const props = defineProps<Props>()

onMounted(() => {
  console.log('[MetricsTable] 组件已挂载, metrics 键数量 =', Object.keys(props.metrics).length)
})

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
      'sharp', 'calmar_ratio', 'win_rate', 'num_trades', 'r_squared',
      'drag_cost_to_return',
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
  {
    title: '协方差诊断',
    keys: [
      'cov_n_assets', 'cov_observations',
      'cov_condition_number', 'cov_positive_definite',
      'cov_min_correlation', 'cov_max_correlation',
      'cov_near_collinear_pairs',
    ],
  },
  {
    title: 'CUSUM 变点检测',
    keys: [
      'cusum_change_points', 'cusum_max_drift', 'cusum_last_change_index',
      'cusum_consensus_count', 'cusum_consensus_triggered',
      'adaptive_var', 'ewma_var',
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
  r_squared: '样本外 R²',
  drag_cost_to_return: '拖累成本/收益比',
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
  // 协方差诊断
  cov_n_assets: '资产数',
  cov_observations: '有效观测数',
  cov_condition_number: '条件数 (κ)',
  cov_positive_definite: '正定性',
  cov_min_correlation: '最小相关系数',
  cov_max_correlation: '最大相关系数',
  cov_near_collinear_pairs: '高相关配对 (|ρ|>0.95)',
  // CUSUM 变点检测
  cusum_change_points: '变点次数',
  cusum_max_drift: '最大漂移量',
  cusum_last_change_index: '最后变点索引',
  cusum_consensus_count: '共识触发资产数',
  cusum_consensus_triggered: '系统性风险',
  adaptive_var: '自适应 VaR',
  ewma_var: 'EWMA VaR',
}

// === 格式化 ===

function formatMetricValue(key: string, value: number | null | undefined): string {
  // 空值处理
  if (value === null || value === undefined) {
    return '-'
  }

  // === 特殊处理 ===
  // Bootstrap 方法
  if (key === 'boot_method') {
    return value === 0 ? '标准' : 'Block'
  }
  // 整数类型
  if (key === 'boot_block_size' || key === 'boot_n_simulations' || key === 'num_trades') {
    return Math.round(value).toString()
  }
  // 自相关系数（保留4位小数）
  if (key === 'boot_autocorrelation') {
    return value.toFixed(4)
  }

  // 样本外 R²（保留4位小数）
  if (key === 'r_squared') {
    return value.toFixed(4)
  }

  // 协方差诊断
  if (key === 'cov_n_assets' || key === 'cov_observations' || key === 'cov_near_collinear_pairs') {
    return Math.round(value).toString()
  }
  if (key === 'cov_positive_definite') {
    return value > 0.5 ? '✓ 是' : '✗ 否'
  }
  if (key === 'cov_condition_number') {
    return value < 1000 ? value.toFixed(2) : value.toFixed(0)
  }
  if (key === 'cov_min_correlation' || key === 'cov_max_correlation') {
    return value.toFixed(4)
  }

  // === 夏普比率：小数显示，保留两位小数 ===
  if (key === 'sharp' || key === 'calmar_ratio' || key === 'information_ratio') {
    return value.toFixed(2)
  }

  // === 拖累成本比（百分比） ===
  if (key === 'drag_cost_to_return') {
    return `${(value * 100).toFixed(2)}%`
  }

  // === 百分比显示的指标 ===
  
  // 收益率类：年化、总收益、压力测试收益率、Bootstrap 收益率
  const returnKeys = ['annual_return', 'total_return', 'stress_return', 'boot_return', 'median_annual_ret']
  const isReturn = returnKeys.some(k => key.includes(k))
  if (isReturn) {
    return `${(value * 100).toFixed(2)}%`
  }

  // 回撤类：最大回撤、压力测试回撤、Bootstrap 最大回撤
  const drawdownKeys = ['max_drawdown', 'stress_max_dd', 'boot_max_dd', 'tail_1pct_avg_dd']
  const isDrawdown = drawdownKeys.some(k => key.includes(k))
  if (isDrawdown) {
    return `${(value * 100).toFixed(2)}%`
  }

  // 波动率
  if (key === 'volatility') {
    return `${(value * 100).toFixed(2)}%`
  }

  // 胜率
  if (key === 'win_rate') {
    return `${(value * 100).toFixed(2)}%`
  }

  // Alpha / Beta
  if (key === 'alpha' || key === 'beta') {
    return `${(value * 100).toFixed(2)}%`
  }

  // 风险价值 / 预期短缺
  if (key === 'VAR' || key === 'ES') {
    return `${(value * 100).toFixed(2)}%`
  }

  // CUSUM VaR 类
  if (key === 'adaptive_var' || key === 'ewma_var') {
    return `${(value * 100).toFixed(2)}%`
  }

  // CUSUM 整数类型
  if (key === 'cusum_change_points' || key === 'cusum_last_change_index' || key === 'cusum_consensus_count') {
    return Math.round(value).toString()
  }

  // CUSUM 漂移量
  if (key === 'cusum_max_drift') {
    return value.toFixed(4)
  }

  // CUSUM 系统性风险（布尔值）
  if (key === 'cusum_consensus_triggered') {
    return value > 0.5 ? '⚠ 触发' : '✓ 正常'
  }

  // 爆仓概率类
  if (key.includes('ruin_prob') || key.includes('stress_ruin')) {
    return `${(value * 100).toFixed(2)}%`
  }

  // === 默认：小数显示，保留两位小数 ===
  return value.toFixed(2)
}

// === 样式辅助 ===

function getValueClass(key: string, value: number | null | undefined): string {
  // 空值无样式类
  if (value === null || value === undefined) {
    return ''
  }

  const lowerKey = key.toLowerCase()

  // === 爆仓概率类：> 5% 标红 ===
  const ruinProbKeys = ['ruin_prob', 'stress_ruin']
  const isRuinProb = ruinProbKeys.some(k => lowerKey.includes(k))
  if (isRuinProb && value > 0.05) return 'value-negative'

  // === 压力测试负面结果：收益率 < -10% 或 回撤 > 15% 标红 ===
  const isStressReturn = lowerKey.includes('stress_return') && value < -0.1
  if (isStressReturn) return 'value-negative'

  const isStressDrawdown = lowerKey.includes('stress_max_dd') && value > 0.15
  if (isStressDrawdown) return 'value-negative'

  // === 极端亏损：年化收益 < -20% 标红 ===
  const isExtremeLoss = lowerKey.includes('annual_return') && value < -0.2
  if (isExtremeLoss) return 'value-negative'

  // === 极端亏损：最大回撤 > 20% 标红 ===
  const isExtremeDrawdown = lowerKey.includes('max_drawdown') && value > 0.2
  if (isExtremeDrawdown) return 'value-negative'

  // === 收益率分布尾部：P5 < -20% 标红 ===
  const isTailLoss = lowerKey.includes('return_p5') && value < -0.2
  if (isTailLoss) return 'value-negative'

  // === 样本外 R² < 0 标红（拟合劣于均值，策略噪声大） ===
  if (lowerKey === 'r_squared' && value < 0) return 'value-negative'

  // === 拖累成本比：>50% 标红（摩擦成本吃掉一半以上收益） ===
  if (lowerKey === 'drag_cost_to_return' && value > 0.5) return 'value-negative'

  // === 协方差诊断 ===
  if (lowerKey === 'cov_condition_number' && value >= 1000) return 'value-negative'
  if (lowerKey === 'cov_positive_definite' && value < 0.5) return 'value-negative'
  if (lowerKey === 'cov_max_correlation' && value > 0.95) return 'value-negative'
  // 有效观测数/资产数 < 5 标红（数据不充分）
  if (lowerKey === 'cov_observations') {
    const nAssets = props.metrics['cov_n_assets'] ?? 2
    if (value < nAssets * 5) return 'value-negative'
  }

  // === CUSUM 变点检测 ===
  // 系统性风险触发标红
  if (lowerKey === 'cusum_consensus_triggered' && value > 0.5) return 'value-negative'
  // 变点次数过多（>5 次）标红
  if (lowerKey === 'cusum_change_points' && value > 5) return 'value-negative'
  // 自适应 VaR > 5% 标红（日风险超过 5%）
  if (lowerKey === 'adaptive_var' && value > 0.05) return 'value-negative'

  // === 其他所有指标：默认颜色 ===
  return ''
}

// === 计算属性 ===

interface MetricItem {
  key: string
  name: string
  value: number | null | undefined
  displayValue: string
}

interface GroupedMetrics {
  title: string
  items: MetricItem[]
}

const metricGroups = computed<GroupedMetrics[]>(() => {
  const result = metricGroupsDef.map(group => {
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

  console.log('[MetricsTable] metricGroups 计算: 总分组数 =', result.length, '总指标数 =', result.reduce((sum, g) => sum + g.items.length, 0))

  return result
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

/* === 空状态 === */

.empty-state {
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  padding: 60px 20px;
  color: var(--text-secondary, #a0aec0);
}

.empty-icon {
  font-size: 48px;
  margin-bottom: 12px;
  opacity: 0.5;
}

.empty-text {
  font-size: 16px;
  font-weight: 500;
  margin-bottom: 8px;
}

.empty-hint {
  font-size: 13px;
  opacity: 0.7;
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
