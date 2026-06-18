/**
 * 策略标的选择器 Composable
 * 从 IndexedDB 中的策略流程图解析标的池，供波动率分析/信号分析等面板共用
 */
import { ref, computed, watch, onMounted } from 'vue'
import { useHistoryStore } from '@/stores/history'
import { extractSecuritiesFromFlowData, type FlowData, type Security } from '@/lib/strategyPool'

export function useStrategySecurities() {
  const historyStore = useHistoryStore()

  // 选中的策略 ID
  const selectedStrategyId = ref('')
  // 当前版本的标的池
  const availableSecurities = ref<Security[]>([])
  // 用户勾选的标的代码集合
  const checkedSymbols = ref<Set<string>>(new Set())
  // 加载状态
  const loading = ref(false)

  // 策略列表选项
  const strategyOptions = computed(() =>
    historyStore.strategies.map(s => ({ id: s.id, name: s.name }))
  )

  /** 加载指定策略最新版本的标的池 */
  async function loadSecuritiesForStrategy(strategyId: string) {
    selectedStrategyId.value = strategyId
    loading.value = true
    checkedSymbols.value = new Set()
    try {
      const versions = historyStore.getVersionsByStrategy(strategyId)
      if (versions.length === 0) {
        availableSecurities.value = []
        return
      }
      // 取最新版本
      const sorted = [...versions].sort((a, b) =>
        new Date(b.saveTime).getTime() - new Date(a.saveTime).getTime()
      )
      const latestVersion = sorted[0]

      const flowData = await historyStore.loadVersionFlowData(latestVersion.id)
      if (flowData) {
        const securities = extractSecuritiesFromFlowData(flowData as unknown as FlowData)
        availableSecurities.value = securities
        // 默认全选
        checkedSymbols.value = new Set(securities.map(s => s.code))
      } else {
        availableSecurities.value = []
      }
    } finally {
      loading.value = false
    }
  }

  /** 切换标的勾选状态 */
  function toggleSymbol(code: string) {
    const next = new Set(checkedSymbols.value)
    if (next.has(code)) {
      next.delete(code)
    } else {
      next.add(code)
    }
    checkedSymbols.value = next
  }

  /** 获取当前已勾选的标的代码列表 */
  function getCheckedSymbols(): string[] {
    return Array.from(checkedSymbols.value)
  }

  /** 全选 */
  function checkAll() {
    checkedSymbols.value = new Set(availableSecurities.value.map(s => s.code))
  }

  /** 清空勾选 */
  function clearChecked() {
    checkedSymbols.value = new Set()
  }

  return {
    strategyOptions,
    selectedStrategyId,
    availableSecurities,
    checkedSymbols,
    loading,
    loadSecuritiesForStrategy,
    toggleSymbol,
    getCheckedSymbols,
    checkAll,
    clearChecked
  }
}
