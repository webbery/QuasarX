/**
 * UI 类型注册（组件侧）
 *
 * 契约在 lib/charts/uiTypes.ts（不含 .vue），这里负责：
 *   1. 把契约登记进注册表
 *   2. 给每个类型补上渲染组件
 *
 * 幂等 —— 重复调用只生效一次。
 */

import { registerUiType } from '@/lib/charts/registry'
import { UI_TYPE_DEFS } from '@/lib/charts/uiTypes'
import TimeSeriesView from './TimeSeriesView.vue'
import PerformanceView from './PerformanceView.vue'
import PriceTrendView from './PriceTrendView.vue'
import MonteCarloView from './MonteCarloView.vue'
import HistogramView from './HistogramView.vue'
import MatrixView from './MatrixView.vue'
import TableView from './TableView.vue'
import MetricView from './MetricView.vue'
import ProgressView from './ProgressView.vue'

let registered = false

/** id → 渲染组件 */
const COMPONENTS: Record<string, unknown> = {
  timeseries: TimeSeriesView,
  performance: PerformanceView,
  price: PriceTrendView,
  mc_paths: MonteCarloView,
  histogram: HistogramView,
  matrix: MatrixView,
  table: TableView,
  metric: MetricView,
  progress: ProgressView,
}

export function registerUiTypes(): void {
  if (registered) return
  registered = true

  for (const def of UI_TYPE_DEFS) {
    registerUiType({ ...def, component: (COMPONENTS[def.id] ?? null) as any })
  }
}
