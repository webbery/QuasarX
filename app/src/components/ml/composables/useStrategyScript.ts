// app/src/components/ml/composables/useStrategyScript.ts
// 从 IndexedDB 取策略最新版本的流程图，供 ML 分析与特征门禁共用。
// 只负责「策略 → 图 JSON」，各面板自己决定拿到图之后做什么。

import { ref } from 'vue'
import { useHistoryStore } from '@/stores/history'

/** 被当作 ML 节点的 nodeType（与 MLTab / MLHandler 的判定保持一致） */
export const ML_NODE_TYPES = ['xgboost', 'ml', 'onnx_inference'] as const

export interface StrategyScriptResult {
  script: string
  graph: any
  backtest: { start?: string; end?: string }
  xgbLabels: string[]
  hasMLNode: boolean
}

export function useStrategyScript() {
  const historyStore = useHistoryStore()

  const script = ref('')
  const graph = ref<any>(null)
  const loading = ref(false)
  const hasMLNode = ref(false)
  const backtest = ref<{ start?: string; end?: string }>({})
  const xgbLabels = ref<string[]>([])

  function reset() {
    script.value = ''
    graph.value = null
    hasMLNode.value = false
    backtest.value = {}
    xgbLabels.value = []
  }

  /** 载入策略最新版本的流程图；失败或无版本时清空并返回 null */
  async function load(strategyId: string): Promise<StrategyScriptResult | null> {
    if (!strategyId) {
      reset()
      return null
    }

    loading.value = true
    try {
      const versions = historyStore.getVersionsByStrategy(strategyId)
      if (versions.length === 0) {
        reset()
        return null
      }

      const sorted = [...versions].sort((a, b) =>
        new Date(b.saveTime).getTime() - new Date(a.saveTime).getTime()
      )
      const data = await historyStore.loadVersionFlowData(sorted[0].id)
      if (!data) {
        reset()
        return null
      }

      const g: any = typeof data === 'string' ? JSON.parse(data) : data
      g.id = strategyId

      const nodes: any[] = Array.isArray(g.nodes) ? g.nodes : []
      const mlNodes = nodes.filter(n => (ML_NODE_TYPES as readonly string[]).includes(n?.data?.nodeType))

      graph.value = g
      script.value = JSON.stringify(g)
      backtest.value = g.backtest || {}
      hasMLNode.value = mlNodes.length > 0
      xgbLabels.value = mlNodes.map(n => n?.data?.label || '')

      return {
        script: script.value,
        graph: g,
        backtest: backtest.value,
        xgbLabels: xgbLabels.value,
        hasMLNode: hasMLNode.value,
      }
    } catch (e) {
      console.error('[ML] 加载策略失败:', e)
      reset()
      return null
    } finally {
      loading.value = false
    }
  }

  return {
    script,
    graph,
    loading,
    hasMLNode,
    backtest,
    xgbLabels,
    load,
    reset,
  }
}
