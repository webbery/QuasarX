import { ref, watch } from 'vue'

const STORAGE_KEY = 'visual-analysis-panels'

export interface AnalysisPanelItem {
  id: string
  label: string
  enabled: boolean
}

const defaultPanels: AnalysisPanelItem[] = [
  { id: 'volatility', label: '波动率分析', enabled: true },
  { id: 'signal', label: '信号分析', enabled: true },
  { id: 'pca', label: 'PCA 主成分', enabled: true },
  { id: 'cusum', label: 'CUSUM 结构变化', enabled: true },
  { id: 'xgboost', label: 'XGBoost 分析', enabled: false },
  { id: 'fundamental', label: '基本面分析', enabled: true },
  { id: 'flow', label: '资金流向', enabled: true },
  { id: 'strategyData', label: '策略数据', enabled: true },
]

function loadConfig(): AnalysisPanelItem[] {
  try {
    const raw = localStorage.getItem(STORAGE_KEY)
    if (!raw) return defaultPanels.map(p => ({ ...p }))
    const map: Record<string, boolean> = JSON.parse(raw)
    return defaultPanels.map(p => ({
      ...p,
      enabled: map[p.id] !== undefined ? map[p.id] : p.enabled,
    }))
  } catch {
    return defaultPanels.map(p => ({ ...p }))
  }
}

function saveConfig(panels: AnalysisPanelItem[]) {
  const map: Record<string, boolean> = {}
  for (const p of panels) map[p.id] = p.enabled
  localStorage.setItem(STORAGE_KEY, JSON.stringify(map))
}

const panels = ref<AnalysisPanelItem[]>(loadConfig())

watch(panels, (val) => saveConfig(val), { deep: true })

export function useAnalysisPanelConfig() {
  function toggle(id: string) {
    const panel = panels.value.find(p => p.id === id)
    if (panel) panel.enabled = !panel.enabled
  }

  return { panels, toggle }
}
