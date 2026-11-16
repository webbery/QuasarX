// app/src/lib/echartsTheme.ts
// ECharts 暗色主题注册（全应用共享，避免各组件重复注册导致告警）

import * as echarts from 'echarts'

export const DARK_THEME_NAME = 'quasarx-dark'

const DARK_THEME = {
  backgroundColor: 'transparent',
  textStyle: { color: '#e0e0e0' },
  title: { textStyle: { color: '#e0e0e0' } },
  line: {
    itemStyle: { borderWidth: 1 },
    lineStyle: { width: 2 },
    symbolSize: 4,
    symbol: 'emptyCircle',
    smooth: false,
  },
  color: ['#2962ff', '#ff9800', '#00c853', '#ff6d00', '#a0aec0'],
}

let registered = false

/** 注册 QuasarX 暗色主题（幂等，可重复调用） */
export function registerDarkTheme(): void {
  if (registered) return
  registered = true
  try {
    echarts.registerTheme(DARK_THEME_NAME, DARK_THEME)
  } catch {
    // 主题已注册，忽略
  }
}
