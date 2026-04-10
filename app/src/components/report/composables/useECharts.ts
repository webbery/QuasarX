// app/src/components/report/composables/useECharts.ts
// 通用 ECharts hook - 处理初始化、主题、响应式

import * as echarts from 'echarts'
import { ref, shallowRef, onMounted, onUnmounted, type Ref, nextTick } from 'vue'

// 暗色主题配置
const DARK_THEME = {
  backgroundColor: 'transparent',
  textStyle: { color: '#e0e0e0' },
  title: { textStyle: { color: '#e0e0e0' } },
  line: {
    itemStyle: { borderWidth: 1 },
    lineStyle: { width: 2 },
    symbolSize: 4,
    symbol: 'emptyCircle',
    smooth: false
  },
  color: ['#2962ff', '#ff9800', '#00c853', '#ff6d00', '#a0aec0']
}

export interface UseEChartsReturn {
  /** ECharts 实例 */
  chart: Ref<echarts.EChartsType | null>
  /** 图表 DOM 容器 */
  chartRef: Ref<HTMLElement | null>
  /** 初始化图表 */
  initChart: () => void
  /** 更新图表配置 */
  updateChart: (option: any, notMerge?: boolean) => void
  /** 销毁图表 */
  dispose: () => void
  /** 手动触发 resize */
  resize: () => void
}

/**
 * ECharts 通用 Hook
 * @param autoInit 是否自动初始化（默认 true）
 */
export function useECharts(autoInit = true): UseEChartsReturn {
  const chartRef = ref<HTMLElement | null>(null)
  const chart = shallowRef<echarts.EChartsType | null>(null)
  let resizeObserver: ResizeObserver | null = null

  /**
   * 初始化图表实例
   */
  function initChart() {
    if (!chartRef.value) {
      console.warn('[useECharts] chartRef 未绑定，无法初始化')
      return
    }

    // 如果已存在实例，先销毁
    if (chart.value) {
      chart.value.dispose()
    }

    // 注册暗色主题
    try {
      echarts.registerTheme('quasarx-dark', DARK_THEME)
    } catch {
      // 主题可能已注册，忽略错误
    }

    chart.value = echarts.init(chartRef.value, 'quasarx-dark')
    
    // 监听容器大小变化
    resizeObserver = new ResizeObserver(() => {
      chart.value?.resize()
    })
    resizeObserver.observe(chartRef.value)

    console.info('[useECharts] 图表已初始化')
  }

  /**
   * 更新图表配置
   * @param option ECharts 配置对象
   * @param notMerge 是否不合并配置（默认 false）
   */
  function updateChart(option: any, notMerge = false) {
    if (!chart.value) {
      console.warn('[useECharts] 图表未初始化，无法更新')
      return
    }
    chart.value.setOption(option, notMerge)
  }

  /**
   * 销毁图表实例
   */
  function dispose() {
    if (resizeObserver) {
      resizeObserver.disconnect()
      resizeObserver = null
    }
    if (chart.value) {
      chart.value.dispose()
      chart.value = null
    }
  }

  /**
   * 手动触发 resize
   */
  function resize() {
    chart.value?.resize()
  }

  // 自动初始化
  if (autoInit) {
    onMounted(() => {
      nextTick(() => {
        initChart()
      })
    })
  }

  onUnmounted(() => {
    dispose()
  })

  return {
    chart,
    chartRef,
    initChart,
    updateChart,
    dispose,
    resize
  }
}

/**
 * 创建图表选项的基础配置（暗色主题）
 */
export function createBaseChartOption(overrides: any = {}): any {
  return {
    tooltip: {
      trigger: 'axis',
      backgroundColor: 'rgba(26, 34, 54, 0.9)',
      borderColor: '#2a3449',
      textStyle: { color: '#e0e0e0' }
    },
    grid: {
      left: '3%',
      right: '4%',
      bottom: '3%',
      containLabel: true
    },
    ...overrides
  }
}
