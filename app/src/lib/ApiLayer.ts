/**
 * API 执行层
 * 根据意图路由结果，从对应的 Pinia store 获取数据，格式化为文本注入到 prompt
 * 支持多步执行（executePipeline），步骤间通过变量传递数据
 */

import { useQuoteStore } from '@/stores/quoteStore'
import { useChatStore } from '@/stores/chatStore'
import { builtinFunctions } from './BuiltinFunctions'
import type { IntentMatch } from './IntentRouter'

export interface ApiResult {
  api: string
  data: any
  formatted: string  // 格式化后的文本，用于注入 prompt
}

export interface IntentStep {
  match: IntentMatch
  resolvedParams: Record<string, any>
}

export class ApiLayer {
  /**
   * 执行单步意图
   */
  async execute(api: string, params: Record<string, any>): Promise<ApiResult> {
    switch (api) {
      case 'system':
        return this.execSystem(params)
      case 'quote':
        return this.execQuote(params)
      case 'account':
        return this.execAccount(params)
      case 'position':
        return this.execPosition(params)
      default:
        throw new Error(`Unknown api: ${api}`)
    }
  }

  /**
   * 执行多步意图管道（按顺序执行，步骤间传递变量）
   */
  async executePipeline(steps: IntentStep[]): Promise<ApiResult> {
    if (steps.length === 0) {
      throw new Error('No steps to execute')
    }

    const vars: Record<string, any> = {}
    let lastResult: ApiResult | null = null

    for (const step of steps) {
      // 解析参数中的 $变量名
      const resolved = this.resolveParams(step.match.rule.params, vars)

      console.log(
        `[ApiLayer] 执行步骤: ${step.match.rule.id} (${step.match.rule.api})`,
        resolved
      )

      const result = await this.execute(step.match.rule.api, resolved)
      lastResult = result

      // 将结果存入变量
      for (const key of step.match.rule.provides) {
        vars[key] = result.data
      }
    }

    return lastResult!
  }

  /**
   * 解析参数：将 "$varName" 替换为变量表中的实际值
   */
  private resolveParams(
    params: Record<string, any>,
    vars: Record<string, any>
  ): Record<string, any> {
    const resolved: Record<string, any> = {}

    for (const [key, val] of Object.entries(params)) {
      if (typeof val === 'string' && val.startsWith('$')) {
        const varName = val.slice(1)
        resolved[key] = vars[varName] !== undefined ? vars[varName] : val
      } else {
        resolved[key] = val
      }
    }

    return resolved
  }

  /**
   * 调用内置 JS 函数
   */
  private async execSystem(params: Record<string, any>): Promise<ApiResult> {
    const fnName = params.fn
    const fn = builtinFunctions[fnName]
    if (!fn) throw new Error(`Unknown builtin function: ${fnName}`)

    const result = await fn()
    return {
      api: 'system',
      data: result,
      formatted: typeof result === 'string' ? result : JSON.stringify(result, null, 2),
    }
  }

  private async execQuote(params: Record<string, any>): Promise<ApiResult> {
    const quoteStore = useQuoteStore()

    let data: any = {}
    let label = ''

    if (params.scope === 'current') {
      // 取实时行情
      if (quoteStore.subscribers.size === 0) {
        quoteStore.subscribe('sh000001')
        quoteStore.subscribe('sh000300')
      }

      const sz = quoteStore.getQuote('sh000001')
      const hs = quoteStore.getQuote('sh000300')

      data = {
        shangzheng: sz,
        hushen300: hs,
        timestamp: new Date().toLocaleString('zh-CN'),
      }
      label = '实时行情'
    } else {
      // 历史行情（$date 已被 resolve 为具体日期）
      data = { scope: params.scope, date: params.date }
      label = '历史行情'
    }

    return {
      api: 'quote',
      data,
      formatted: this.formatQuoteData(data, label),
    }
  }

  private async execAccount(params: Record<string, any>): Promise<ApiResult> {
    const chatStore = useChatStore()
    const data = {
      marketContext: chatStore.marketContext || '暂无数据',
      timestamp: new Date().toLocaleString('zh-CN'),
    }

    return {
      api: 'account',
      data,
      formatted: `【账户风险】\n${data.marketContext}\n数据时间: ${data.timestamp}`,
    }
  }

  private async execPosition(params: Record<string, any>): Promise<ApiResult> {
    const data = {
      scope: params.scope || params.type,
      timestamp: new Date().toLocaleString('zh-CN'),
    }

    return {
      api: 'position',
      data,
      formatted: `【持仓分析】\n暂无详细数据\n数据时间: ${data.timestamp}`,
    }
  }

  private formatQuoteData(data: any, label: string): string {
    const lines = [`【${label}】`]

    if (data.shangzheng) {
      lines.push(`上证指数: ${data.shangzheng.lastPrice ?? '-'} (${data.shangzheng.changePct >= 0 ? '+' : ''}${data.shangzheng.changePct ?? '-'}%)`)
    }
    if (data.hushen300) {
      lines.push(`沪深300: ${data.hushen300.lastPrice ?? '-'} (${data.hushen300.changePct >= 0 ? '+' : ''}${data.hushen300.changePct ?? '-'}%)`)
    }
    lines.push(`数据时间: ${data.timestamp}`)

    return lines.join('\n')
  }
}
