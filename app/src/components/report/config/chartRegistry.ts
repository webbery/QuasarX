// app/src/components/report/config/chartRegistry.ts
// 图表注册表 - 定义所有可用图表的元数据

import type { Component } from 'vue'

export interface ChartDefinition {
  /** 唯一标识 */
  id: string
  /** 显示名称 */
  label: string
  /** 图标 emoji */
  icon: string
  /** 默认是否可见 */
  defaultVisible: boolean
  /** 显示顺序 */
  defaultOrder: number
  /** 布局跨度 */
  span: 'full' | 'half'
  /** 组件懒加载 */
  component: () => Promise<Component>
  /** 描述信息 */
  description?: string
}

/**
 * 所有可用图表的注册表
 * 新增图表只需在此添加一项，配置面板会自动显示
 * 
 * 注意：只注册已迁移完成的图表，渐进式添加
 */
export const CHART_REGISTRY: ChartDefinition[] = [
  {
    id: 'priceTrend',
    label: 'Price Trend & Trading Signals',
    icon: '💹',
    description: '价格趋势与买卖信号',
    defaultVisible: true,
    defaultOrder: 0,
    span: 'full',
    component: () => import('../charts/PriceTrendChart.vue')
  },
  {
    id: 'performance',
    label: 'Strategy Performance',
    icon: '📈',
    description: '策略累计收益 vs 基准（合并）',
    defaultVisible: true,
    defaultOrder: 1,
    span: 'full',
    component: () => import('../charts/PerformanceChart.vue')
  },
  {
    id: 'skewness',
    label: 'Skewness & Kurtosis',
    icon: '⚖️',
    description: '偏度与峰度分析',
    defaultVisible: true,
    defaultOrder: 2,
    span: 'half',
    component: () => import('../charts/SkewnessChart.vue')
  }
  // TODO: 迁移完成后续图表后在此添加
  // - PositionChanges
  // - MonthlyReturnChart
  // - YearlyReturnChart
  // - DistributionReturn
  // - QQPlotChart
  // - PerformanceVsExpect
  // - RollingStatsChart
  // - ReturnQuantiles
  // - DrawdownChart
]

/**
 * 根据 ID 获取图表定义
 */
export function getChartById(id: string): ChartDefinition | undefined {
  return CHART_REGISTRY.find(c => c.id === id)
}

/**
 * 获取所有默认可见的图表（按 order 排序）
 */
export function getDefaultVisibleCharts(): ChartDefinition[] {
  return CHART_REGISTRY
    .filter(c => c.defaultVisible)
    .sort((a, b) => a.defaultOrder - b.defaultOrder)
}
