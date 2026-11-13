/**
 * 内置产物动作处理器
 *
 * 动作是「声明式 + 用户点击」的：Agent 在产物里声明能做什么，
 * 用户点击后才由这里真正执行。
 *
 * P0 接通：
 * - strategy.apply_patch  应用策略修改提案（写 historyStore）
 * - run_backtest          桥接到主界面已有的回测流程
 * - open_panel            切换到指定分析面板
 */

import { registerAction } from './actionRegistry'
import type { PatchArtifact } from './artifacts'
import { applyStrategyUpdate } from '@/lib/tools/strategy'

let registered = false

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

  /* ---------- 执行回测（桥接主界面） ---------- */
  registerAction('run_backtest', async () => {
    window.dispatchEvent(new CustomEvent('agent:run-backtest'))
    return {
      ok: true,
      message: '已请求执行回测，请留意主界面进度提示',
    }
  })

  /* ---------- 打开已有分析面板 ---------- */
  registerAction('open_panel', async (action) => {
    const tab = action.params?.tab as string | undefined
    if (!tab) return { ok: false, message: '缺少 tab 参数' }
    window.dispatchEvent(new CustomEvent('switch-analysis-tab', { detail: { tab } }))
    return { ok: true, message: `已切换到 ${tab} 面板` }
  })

  /* ---------- 检索数据（P0 占位） ---------- */
  registerAction('query_data', async () => {
    return {
      ok: false,
      message: '数据检索动作将在下一阶段接入（当前请直接用自然语言让 Agent 查询）',
    }
  })
}
