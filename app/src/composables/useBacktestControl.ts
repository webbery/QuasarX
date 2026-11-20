// app/src/composables/useBacktestControl.ts
// 回测任务控制（不碰画布）
//
// 分层：
//   lib/api/serviceClient.ts       URL 解析 / 鉴权 / 错误提取（共享底层）
//   composables/useStrategyControl.ts  策略生命周期（远程常驻实例）
//   composables/useBacktestControl.ts  本文件 —— 回测任务（离线评估）
//
// 与 components/strategy/composables/useBacktest.js 的分工：
//   useBacktest.js       依赖画布 state（当前图 / 回测区间 / 资金档），是视图层的执行流程，
//                        还负责把指标推给 ReportView
//   useBacktestControl   只认「策略图 JSON + 区间 + 资金」，是可复用的 API 层，
//                        负责请求、结果归一化、落 historyStore
//
// 回测是长任务（30 秒~数分钟），Agent 的工具层不直接触发（D3-A Phase 5）；
// 但产物上的「执行回测」按钮由用户点击，可以直接走这里，
// 不再需要 CustomEvent + nextTick + setTimeout(150) 的桥接竞态。

import { ref } from 'vue'
import { NOT_LOGGED_IN, apiUrl, authedClient, errorMessage } from '@/lib/api/serviceClient'
import { useHistoryStore, type BacktestResult } from '@/stores/history'

export interface BacktestRunOptions {
  /** 策略图 JSON（含 nodes / edges） */
  script: any
  /** 结果要保存到哪个版本（给了才落库） */
  versionId?: string
  /** 策略 id（用于日志与提示） */
  strategyId?: string
  /** 策略名（用于提示） */
  strategyName?: string
  /**
   * 本次回测的唯一标识，会作为 script.id 发给后端。
   *
   * 后端把 script.id 原样回灌到 backtest_progress 的 strategy 字段，
   * 前端靠它把进度事件关联到「这一次点击」。不传则退回 strategyId。
   */
  runKey?: string
}

export interface BacktestRunResult {
  success: boolean
  message?: string
  /** 归一化后的回测结果 */
  result?: BacktestResult
}

/** 服务端 /v0/backtest 的原始响应（snake_case） */
interface RawBacktestResponse {
  features?: Record<string, number>
  summary?: Record<string, any>
  buy?: any[]
  sell?: any[]
  mc_paths?: any
  daily_returns?: number[]
  daily_dates?: number[]
  protection_events?: any[]
  message?: string
}

/**
 * 把服务端响应归一化成 historyStore 的 BacktestResult 结构。
 *
 * 字段映射与 components/strategy/composables/useBacktest.js 的
 * saveBacktestResultToStore 保持一致，避免两条路径产出的结果形状不同。
 */
export function normalizeBacktestResult(
  raw: RawBacktestResponse,
  script: any,
): BacktestResult {
  const buy = raw.buy || []
  const sell = raw.sell || []
  return {
    backtestTime: new Date().toISOString(),
    features: raw.features || {},
    summary: {
      buy_count: buy.length,
      sell_count: sell.length,
      ...(raw.summary || {}),
    },
    buy,
    sell,
    script,
    mcPaths: raw.mc_paths || undefined,
    dailyReturns: raw.daily_returns || undefined,
    dailyDates: raw.daily_dates || undefined,
    protectionEvents: raw.protection_events || undefined,
  }
}

/**
 * 回测控制。
 *
 * @example
 * const backtest = useBacktestControl()
 * const res = await backtest.run({ script: graph, versionId })
 * if (res.success) console.info(res.message)
 */
export function useBacktestControl() {
  /** 是否有回测正在执行 */
  const running = ref(false)
  /** 最近一次错误 */
  const lastError = ref<string | null>(null)

  /**
   * 执行一次回测。
   *
   * @param options.script       策略图 JSON
   * @param options.versionId    结果保存的目标版本（给了才落 historyStore）
   */
  async function run(options: BacktestRunOptions): Promise<BacktestRunResult> {
    lastError.value = null
    if (!options?.script) {
      return { success: false, message: '缺少策略图 JSON，无法执行回测' }
    }

    const client = authedClient()
    if (!client) {
      return { success: false, message: NOT_LOGGED_IN }
    }

    running.value = true
    try {
      // JSON 模式：body 是 { script: <图 JSON> }，与 useBacktest.js 保持一致。
      //
      // 必须把 id / name 塞进 script：后端拿 script["id"] 当策略标识
      // （回测进度事件里的 strategy 字段、资金池 key、日志都靠它）。
      // 历史版本的 flowData 只有 { nodes, edges }，不补的话后端一律拿到 "unknown"，
      // 进度事件就无法关联到具体策略。
      const script = {
        ...options.script,
        ...(options.runKey
          ? { id: options.runKey }
          : options.strategyId
            ? { id: options.strategyId }
            : {}),
        ...(options.strategyName ? { name: options.strategyName } : {}),
      }
      const response = await client.post(apiUrl('/v0/backtest'), { script })
      const raw = (response.data || {}) as RawBacktestResponse
      const result = normalizeBacktestResult(raw, options.script)

      if (options.versionId) {
        await useHistoryStore().saveBacktestResult(options.versionId, result)
      }

      const buyCount = result.summary.buy_count ?? result.buy.length
      const sellCount = result.summary.sell_count ?? result.sell.length
      const label = options.strategyName ? `「${options.strategyName}」` : ''

      return {
        success: true,
        result,
        message: `回测完成${label}：${buyCount} 笔买入，${sellCount} 笔卖出`,
      }
    } catch (err: any) {
      const message = errorMessage(err, '回测执行失败')
      lastError.value = message
      return { success: false, message }
    } finally {
      running.value = false
    }
  }

  return { running, lastError, run }
}
