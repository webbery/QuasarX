// app/src/components/report/index.ts
// Report 模块统一导出

// Composables
export { useECharts, createBaseChartOption } from './composables/useECharts'
export type { UseEChartsReturn } from './composables/useECharts'

export { useReportState } from './composables/useReportState'
export type { UseReportStateReturn, ReportConfig } from './composables/useReportState'

export { useChartData } from './composables/useChartData'
export type { UseChartDataReturn } from './composables/useChartData'

// Config
export { CHART_REGISTRY, getChartById, getDefaultVisibleCharts } from './config/chartRegistry'
export type { ChartDefinition } from './config/chartRegistry'

// Components
export { default as ReportView } from './ReportView.vue'
export { default as ReportConfigPanel } from './ReportConfigPanel.vue'
export { default as MetricsTable } from './MetricsTable.vue'

// Charts
export { default as PriceTrendChart } from './charts/PriceTrendChart.vue'
export { default as PerformanceChart } from './charts/PerformanceChart.vue'
export { default as SkewnessChart } from './charts/SkewnessChart.vue'
