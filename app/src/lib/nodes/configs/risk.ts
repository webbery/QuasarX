/**
 * 风控保护节点（止损/止盈/追踪/时间）
 */

import { registerNode } from '../registry'
import type { NodeRegistryEntry } from '../types'

export const protectionNode: NodeRegistryEntry = {
  id: 'risk',
  label: '风控保护',
  nodeType: 'protection',
  category: 'risk',
  description: '合并止损、止盈、追踪止损和时间止损四种保护机制。可同时启用，取最先触发的一个。',
  inputs: ['signal'],
  outputs: ['signal'],
  params: [
    { key: 'stop_loss_enabled', label: '止损开关', type: 'boolean', default: false },
    { key: 'stop_loss_percent', label: '止损比例', type: 'number', default: 0.05, min: 0.01, max: 0.5, step: 0.01, unit: '%' },
    { key: 'take_profit_enabled', label: '止盈开关', type: 'boolean', default: false },
    { key: 'take_profit_percent', label: '止盈比例', type: 'number', default: 0.15, min: 0.01, max: 1.0, step: 0.01, unit: '%' },
    { key: 'trailing_stop_enabled', label: '追踪止损开关', type: 'boolean', default: false },
    { key: 'trailing_stop_percent', label: '追踪止损比例', type: 'number', default: 0.03, min: 0.01, max: 0.3, step: 0.01, unit: '%' },
    { key: 'time_stop_enabled', label: '时间止损开关', type: 'boolean', default: false },
    { key: 'max_bars', label: '最大持仓Bar数', type: 'number', default: 20, min: 1, max: 1000, step: 1, unit: '根' },
  ],
  example: {
    stop_loss_enabled: true, stop_loss_percent: 0.05,
    take_profit_enabled: false,
    trailing_stop_enabled: false,
    time_stop_enabled: false
  }
}

registerNode(protectionNode)
