// app/src/components/strategy/stability/stabilityApi.ts
// 稳定性扫描用到的 HTTP 调用
//
// 与 useBacktest.js 保持同一请求形状：JSON 模式下 body 是 { script: <图 JSON 字符串> }，
// script.capital / script.backtest.start-end 都在字符串内部的图里，所以扰动靠改图实现。

import axios from 'axios'
import { applyAuthHeader } from '@/lib/tools/common/auth'

export interface CapitalPoolInfo {
  initialCapital: number
  totalAllocated: number
  totalAvailable: number
  activeStrategies: number
}

/** 读资金池现状，用于生成不超过上限的资金档位 */
export async function fetchCapitalPool(): Promise<CapitalPoolInfo> {
  if (!applyAuthHeader(axios.default)) {
    throw new Error('未登录，无法读取资金池信息')
  }
  const res = await axios.get('/v0/position')
  const capital = res.data?.capital
  if (!capital) throw new Error('资金池信息缺失')
  return {
    initialCapital: Number(capital.initialCapital ?? 0),
    totalAllocated: Number(capital.totalAllocated ?? 0),
    totalAvailable: Number(capital.totalAvailable ?? 0),
    activeStrategies: Number(capital.activeStrategies ?? 0),
  }
}

/** 回测响应中本模块用到的部分 */
export interface BacktestRunResponse {
  status?: string
  message?: string
  features?: Record<string, number>
  summary?: {
    buy_count?: number
    sell_count?: number
    [key: string]: unknown
  }
  buy?: [symbol: string, timestamp: number, quantity: number, price: number][]
  sell?: [symbol: string, timestamp: number, quantity: number, price: number][]
  /** 每日收益率（小数形式） */
  daily_returns?: number[]
  /** 每日对应的 Unix 时间戳（秒） */
  daily_dates?: number[]
}

export async function runBacktestOnce(scriptJson: string): Promise<BacktestRunResponse> {
  if (!applyAuthHeader(axios.default)) {
    throw new Error('未登录，无法运行回测')
  }
  const res = await axios.post('/v0/backtest', { script: scriptJson })
  return res.data as BacktestRunResponse
}
