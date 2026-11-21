/**
 * 内置产物动作处理器
 *
 * 动作是「声明式 + 用户点击」的：Agent 在产物里声明能做什么，
 * 用户点击后才由这里真正执行。
 *
 * P0 接通：
 * - strategy.apply_patch  应用策略修改提案（写 historyStore）
 * - run_backtest          执行回测（走 useBacktestControl，不再靠 CustomEvent 桥接）
 * - open_panel            切换到指定分析面板
 *
 * P1 接通（走 app/src/composables/useStrategyControl.ts 统一 API）：
 * - run_strategy          远程启动策略（service mode: 1）
 * - stop_strategy         远程停止策略（service mode: 2）
 * - deploy_strategy       部署本地策略的最新版本到 service
 */

import { registerAction } from './actionRegistry'
import { makeArtifact, type PatchArtifact } from './artifacts'
import { UI_SPEC_VERSION, type UiSpec } from '@/lib/ui/protocol'
import { applyStrategyUpdate, deleteStrategyById } from '@/lib/tools/strategy'
import { deleteQuoteData, deleteFinanceData } from '@/lib/tools/data'
import { useStrategyControl } from '@/composables/useStrategyControl'
import { useBacktestControl } from '@/composables/useBacktestControl'
import { useHistoryStore } from '@/stores/history'

let registered = false

/**
 * 从 historyStore 取策略最新版本，组装成可部署的策略 JSON。
 *
 * 部署载荷 = { ...graph, data }
 *   - graph: { nodes, edges }
 *   - data:  策略级配置，含 data.models（XGBoost 模型绑定）
 */
function buildDeployPayload(strategyId: string): {
  ok: boolean
  message?: string
  json?: any
  versionLabel?: string
} {
  const history = useHistoryStore()
  const strategy = history.strategies.find((s) => s.id === strategyId)
  if (!strategy) {
    return { ok: false, message: `找不到策略 ${strategyId}` }
  }
  const version = history.getLatestVersion(strategyId)
  if (!version) {
    return { ok: false, message: `策略「${strategy.name}」还没有任何版本，无法部署` }
  }
  const graph = strategy.graph
  if (!graph?.nodes?.length) {
    return { ok: false, message: `策略「${strategy.name}」缺少策略图节点，无法部署` }
  }
  return {
    ok: true,
    json: { ...graph, data: strategy.data ?? {} },
    versionLabel: version.remark || version.id,
  }
}

/** 幂等注册（多次调用只注册一次） */
export function ensureAgentActionsRegistered(): void {
  if (registered) return
  registered = true

  /* ---------- 应用策略修改提案 ---------- */
  registerAction('strategy.apply_patch', async (action, ctx) => {
    const artifact = ctx.artifact as PatchArtifact

    if (artifact.kind !== 'patch') {
      return { ok: false, message: '该操作只能作用于策略修改提案' }
    }

    const payload = artifact.payload
    const strategyId =
      (action.params?.strategyId as string | undefined) || payload?.target?.strategyId

    if (!strategyId) {
      return { ok: false, message: '提案缺少 strategyId，无法应用' }
    }

    if (!payload?.validation?.valid) {
      const errors = payload?.validation?.errors ?? []
      return {
        ok: false,
        message: `提案结构校验未通过，已拒绝应用：${errors.join('；') || '未知错误'}`,
      }
    }

    if (!payload?.graph) {
      return { ok: false, message: '提案缺少策略图数据，无法应用' }
    }

    const result = await applyStrategyUpdate(
      strategyId,
      payload.graph,
      `Agent 提案应用 ${new Date().toLocaleString('zh-CN')}`,
    )

    return { ok: result.ok, message: result.message }
  })

  /* ---------- 删除行情数据（不可逆，需用户点击确认） ---------- */
  registerAction('data_manager.delete_quote', async (action) => {
    const table = action.params?.table as string | undefined
    const symbol = action.params?.symbol as string | undefined
    try {
      const message = await deleteQuoteData({ table, symbol })
      return { ok: true, message }
    } catch (e) {
      const reason = e instanceof Error ? e.message : String(e)
      return { ok: false, message: `删除行情数据失败：${reason}` }
    }
  })

  /* ---------- 删除财务数据（不可逆，需用户点击确认） ---------- */
  registerAction('data_manager.delete_finance', async (action) => {
    const code = action.params?.code as string | undefined
    const category = action.params?.category as string | undefined
    if (!code) {
      return { ok: false, message: '缺少 code 参数，无法删除财务数据' }
    }
    try {
      const message = await deleteFinanceData({ code, category })
      return { ok: true, message }
    } catch (e) {
      const reason = e instanceof Error ? e.message : String(e)
      return { ok: false, message: `删除财务数据失败：${reason}` }
    }
  })

  /* ---------- 删除策略图（不可逆，需用户点击确认） ---------- */
  registerAction('strategy.delete_strategy', async (action) => {
    const strategyId = action.params?.strategyId as string | undefined
    if (!strategyId) {
      return { ok: false, message: '缺少 strategyId 参数，无法删除策略图' }
    }
    try {
      const message = await deleteStrategyById(strategyId)
      return { ok: true, message }
    } catch (e) {
      const reason = e instanceof Error ? e.message : String(e)
      return { ok: false, message: `删除策略图失败：${reason}` }
    }
  })

  /* ---------- 执行回测 ---------- */
  registerAction('run_backtest', async (action, ctx) => {
    const strategyId = action.params?.strategyId as string | undefined
    const versionId = action.params?.versionId as string | undefined
    const strategyName = action.params?.strategyName as string | undefined

    if (!versionId) {
      return { ok: false, message: '缺少 versionId，无法确定要回测的版本' }
    }

    // 从 historyStore 取该版本的策略图
    const history = useHistoryStore()
    let flowData: { nodes: any[]; edges: any[] } | null = null
    try {
      flowData = await history.loadVersionFlowData(versionId)
    } catch (e) {
      return { ok: false, message: `读取版本 ${versionId} 的策略图失败` }
    }
    if (!flowData?.nodes?.length) {
      return { ok: false, message: `版本 ${versionId} 没有可回测的策略图` }
    }

    // 「策略名 + 时间戳」做本次回测的唯一标识：
    // 后端把 script.id 原样回灌到 backtest_progress 的 strategy 字段，
    // 前端据此把事件关联到这一次点击。不能用 strategyId ——
    // 同一策略连续回测时两次的进度会串到同一张卡上。
    const runKey = `${strategyName || strategyId || 'backtest'}@${Date.now()}`

    // 先出进度卡，再阻塞执行 —— 回测是分钟级，等 handler 返回才建卡用户全程看不到反馈
    const title = `回测进度 · ${strategyName ?? strategyId ?? '策略'}`
    const spec: UiSpec = {
      version: UI_SPEC_VERSION,
      type: 'progress',
      title,
      subtitle: `版本 ${versionId}`,
      params: {},
      data: { status: 'running', label: '准备回测…' },
      binding: {
        kind: 'sse',
        event: 'backtest_progress',
        match: { strategy: runKey },
      },
    }
    ctx.emitArtifact?.(
      makeArtifact('ui', title, spec, {
        subtitle: `版本 ${versionId}`,
        source: `backtest · ${runKey}`,
        ui: { height: 150, collapsible: false },
      }),
    )

    const result = await useBacktestControl().run({
      script: flowData,
      versionId,
      strategyId,
      strategyName,
      runKey,
    })
    return { ok: result.success, message: result.message }
  })

  /* ---------- 打开已有分析面板 ---------- */
  registerAction('open_panel', async (action) => {
    const tab = action.params?.tab as string | undefined
    if (!tab) return { ok: false, message: '缺少 tab 参数' }
    window.dispatchEvent(new CustomEvent('switch-analysis-tab', { detail: { tab } }))
    return { ok: true, message: `已切换到 ${tab} 面板` }
  })

  /* ---------- 远程启动策略 ---------- */
  registerAction('run_strategy', async (action) => {
    const name = action.params?.name as string | undefined
    if (!name) return { ok: false, message: '缺少 name 参数（策略名）' }

    const result = await useStrategyControl().start(name)
    return { ok: result.success, message: result.message }
  })

  /* ---------- 远程停止策略 ---------- */
  registerAction('stop_strategy', async (action) => {
    const name = action.params?.name as string | undefined
    if (!name) return { ok: false, message: '缺少 name 参数（策略名）' }

    const result = await useStrategyControl().stop(name)
    return { ok: result.success, message: result.message }
  })

  /* ---------- 部署本地策略的最新版本 ---------- */
  registerAction('deploy_strategy', async (action) => {
    const strategyId = action.params?.strategyId as string | undefined
    const name = action.params?.name as string | undefined
    if (!strategyId && !name) {
      return { ok: false, message: '缺少 strategyId 或 name 参数' }
    }

    const payload = strategyId
      ? buildDeployPayload(strategyId)
      : { ok: false, message: '部署需要 strategyId（本地策略 id）' }

    if (!payload.ok || !payload.json) {
      return { ok: false, message: payload.message }
    }

    const history = useHistoryStore()
    const strategy = history.strategies.find((s) => s.id === strategyId)
    const strategyName = name || strategy?.name || strategyId!

    const result = await useStrategyControl().deploy(strategyName, payload.json)
    if (!result.success && result.conflict) {
      return {
        ok: false,
        message: `${result.message}（可先停止该策略，或用强制部署）`,
      }
    }
    return { ok: result.success, message: result.message }
  })
}
