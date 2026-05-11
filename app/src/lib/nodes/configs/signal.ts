/**
 * 交易信号生成节点
 */

import { registerNode } from '../registry'
import type { NodeRegistryEntry } from '../types'

export const signalGenerationNode: NodeRegistryEntry = {
  id: 'signal-generation',
  label: '交易信号生成',
  nodeType: 'signal',
  category: 'signal',
  description: '根据指标或公式生成买卖信号。支持自定义买入/卖出条件表达式。',
  inputs: ['indicator', 'timeseries'],
  outputs: ['signal'],
  hasInput: true,
  hasOutput: false,
  params: [
    { key: 'type', label: '类型', type: 'select', default: '股票', options: ['股票', '期货', '期权'] },
    { key: 'code', label: '代码', type: 'text', default: '', visible: false },
    { key: 'buy', label: '买入条件', type: 'text', default: 'MA_5-MA_15 >= 0' },
    { key: 'sell', label: '卖出条件', type: 'text', default: 'MA_5-MA_15 < 0' },
    { key: 'allowShort', label: '允许做空', type: 'boolean', default: false },
  ],
  example: { type: '股票', buy: 'MA_5-MA_15 >= 0', sell: 'MA_5-MA_15 < 0' }
}

registerNode(signalGenerationNode)
