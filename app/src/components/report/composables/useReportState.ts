// app/src/components/report/composables/useReportState.ts
// 状态管理 - 集中管理所有响应式状态和配置

import { ref, computed, watch, type Ref, type ComputedRef } from 'vue'
import { CHART_REGISTRY, type ChartDefinition } from '../config/chartRegistry'

const CONFIG_STORAGE_KEY = 'quasarx_report_config'

export interface ReportConfig {
  /** 图表可见性配置 */
  chartVisibility: Record<string, boolean>
  /** 默认基准 */
  defaultBenchmark: string
  /** 是否显示指标表格 */
  showMetricsTable: boolean
}

export interface UseReportStateReturn {
  // === 核心状态 ===
  /** 选中的标的（支持多个） */
  selectedSymbol: Ref<string[]>
  /** 选中的基准代码 */
  selectedBenchmark: Ref<string>
  /** 回测指标数据 */
  metricsData: Ref<Record<string, number>>
  /** 策略性能图表的日期标签 */
  strategyPerformanceDates: Ref<string[]>
  /** 策略性能数据 */
  strategyPerformanceData: Ref<number[]>
  /** 基准数据 */
  benchmarkData: Ref<any[]>
  /** 基准名称 */
  benchmarkName: Ref<string>
  /** 回测开始日期 */
  backtestStartDate: Ref<Date | null>
  /** 回测结束日期 */
  backtestEndDate: Ref<Date | null>

  // === 图表配置 ===
  /** 图表可见性配置（响应式） */
  chartVisibility: Ref<Record<string, boolean>>
  /** 是否显示指标表格 */
  showMetricsTable: Ref<boolean>
  /** 当前可见的图表列表（按 order 排序） */
  visibleCharts: ComputedRef<ChartDefinition[]>

  // === 配置管理 ===
  /** 加载配置（从 localStorage） */
  loadConfig: () => ReportConfig
  /** 保存配置（到 localStorage） */
  saveConfig: () => void
  /** 重置配置（恢复默认） */
  resetConfig: () => void
  /** 切换图表可见性 */
  toggleChartVisibility: (chartId: string) => void
  /** 全选/取消全选 */
  setAllChartsVisible: (visible: boolean) => void
}

/**
 * 默认配置
 */
function getDefaultConfig(): ReportConfig {
  const chartVisibility: Record<string, boolean> = {}
  CHART_REGISTRY.forEach(chart => {
    chartVisibility[chart.id] = chart.defaultVisible
  })
  return {
    chartVisibility,
    defaultBenchmark: localStorage.getItem('benchmark_symbol') || 'SH000001',
    showMetricsTable: true
  }
}

/**
 * 从 localStorage 加载配置
 */
function loadConfigFromStorage(): ReportConfig {
  try {
    const stored = localStorage.getItem(CONFIG_STORAGE_KEY)
    if (stored) {
      const parsed = JSON.parse(stored) as Partial<ReportConfig>
      const defaultConfig = getDefaultConfig()
      // 合并默认配置，确保新增图表也能显示
      return {
        chartVisibility: { ...defaultConfig.chartVisibility, ...parsed.chartVisibility },
        defaultBenchmark: parsed.defaultBenchmark ?? defaultConfig.defaultBenchmark,
        showMetricsTable: parsed.showMetricsTable ?? defaultConfig.showMetricsTable
      }
    }
  } catch (e) {
    console.warn('[useReportState] 加载配置失败，使用默认配置', e)
  }
  return getDefaultConfig()
}

/**
 * 保存配置到 localStorage
 */
function saveConfigToStorage(config: ReportConfig) {
  try {
    localStorage.setItem(CONFIG_STORAGE_KEY, JSON.stringify(config))
  } catch (e) {
    console.warn('[useReportState] 保存配置失败', e)
  }
}

/**
 * 报告状态管理 Hook
 */
export function useReportState(): UseReportStateReturn {
  // === 核心状态 ===
  const selectedSymbol = ref<string[]>([])
  const selectedBenchmark = ref(localStorage.getItem('benchmark_symbol') || 'SH000001')
  const metricsData = ref<Record<string, number>>({})
  const strategyPerformanceDates = ref<string[]>([])
  const strategyPerformanceData = ref<number[]>([])
  const benchmarkData = ref<any[]>([])
  const benchmarkName = ref('')
  const backtestStartDate = ref<Date | null>(null)
  const backtestEndDate = ref<Date | null>(null)

  // === 图表配置 ===
  const initialConfig = loadConfigFromStorage()
  const chartVisibility = ref<Record<string, boolean>>(initialConfig.chartVisibility)
  const showMetricsTable = ref(initialConfig.showMetricsTable)

  // === 计算属性 ===
  const visibleCharts = computed<ChartDefinition[]>(() => {
    return CHART_REGISTRY
      .filter(chart => chartVisibility.value[chart.id])
      .sort((a, b) => a.defaultOrder - b.defaultOrder)
  })

  // === 配置管理方法 ===
  function loadConfig(): ReportConfig {
    const config = loadConfigFromStorage()
    chartVisibility.value = config.chartVisibility
    selectedBenchmark.value = config.defaultBenchmark
    return config
  }

  function saveConfig(): void {
    const config: ReportConfig = {
      chartVisibility: chartVisibility.value,
      defaultBenchmark: selectedBenchmark.value,
      showMetricsTable: showMetricsTable.value
    }
    saveConfigToStorage(config)
  }

  function resetConfig(): void {
    const defaultConfig = getDefaultConfig()
    chartVisibility.value = defaultConfig.chartVisibility
    selectedBenchmark.value = defaultConfig.defaultBenchmark
    showMetricsTable.value = defaultConfig.showMetricsTable
    saveConfig()
  }

  function toggleChartVisibility(chartId: string): void {
    chartVisibility.value[chartId] = !chartVisibility.value[chartId]
    saveConfig()
  }

  function setAllChartsVisible(visible: boolean): void {
    CHART_REGISTRY.forEach(chart => {
      chartVisibility.value[chart.id] = visible
    })
    saveConfig()
  }

  // === 自动保存配置（当关键状态变化时） ===
  watch(selectedBenchmark, (val) => {
    localStorage.setItem('benchmark_symbol', val)
    saveConfig()
  })

  watch(chartVisibility, () => {
    saveConfig()
  }, { deep: true })

  return {
    // 核心状态
    selectedSymbol,
    selectedBenchmark,
    metricsData,
    strategyPerformanceDates,
    strategyPerformanceData,
    benchmarkData,
    benchmarkName,
    backtestStartDate,
    backtestEndDate,
    // 图表配置
    chartVisibility,
    showMetricsTable,
    visibleCharts,
    // 配置管理
    loadConfig,
    saveConfig,
    resetConfig,
    toggleChartVisibility,
    setAllChartsVisible
  }
}
