/**
 * 包络突破状态机节点
 *
 * 4 态状态机：突破上轨(1) / 回落上轨内(2) / 突破下轨(3) / 回落下轨内(4)
 * 输入：当前价格 + 上轨 + 下轨（来自 FormulaNode 的 MA±k×STD）
 * 输出：状态编码 + 持续 bar 数，供 XGBoost 元学习器使用
 */

import { registerNode } from '../registry'
import type { NodeRegistryEntry } from '../types'

/**
 * BreakoutNode 的命名输入槽位
 * slot: handle 后缀（前端 handle ID = "input-{slot}"）
 * field: 期望的数据字段（仅用于显示）
 */
export const breakoutInputSlots = [
  { slot: 'value', field: 'close', label: '当前价格' },
  { slot: 'upper', field: 'upper', label: '上轨' },
  { slot: 'lower', field: 'lower', label: '下轨' },
]

export const breakoutNode: NodeRegistryEntry = {
  id: 'breakout',
  label: '突破检测',
  nodeType: 'breakout',
  category: 'process',
  description: '包络突破状态机（4 态，含 hysteresis）。检测价格对上下轨的突破与回落，输出状态编码供 XGBoost 学习。状态 1=突破上轨, 2=回落上轨内, 3=突破下轨, 4=回落下轨内。',
  inputs: ['value', 'upper', 'lower'],
  outputs: ['state', 'duration'],
  params: [],
  example: {}
}

registerNode(breakoutNode)
