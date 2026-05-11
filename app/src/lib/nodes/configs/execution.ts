/**
 * 交易执行节点
 */

import { registerNode } from '../registry'
import type { NodeRegistryEntry } from '../types'

export const executionNode: NodeRegistryEntry = {
  id: 'execution',
  label: '执行交易',
  nodeType: 'execution',
  category: 'execution',
  description: '接收交易信号后执行实际下单操作。可配置佣金费率、印花税、滑点等交易成本。',
  inputs: ['signal'],
  outputs: [],
  hasInput: true,
  hasOutput: false,
  params: [
    { key: 'commission', label: '佣金费率', type: 'number', default: 0.0003, min: 0, max: 0.01, step: 0.0001, unit: '%' },
    { key: 'stampDuty', label: '印花税率', type: 'number', default: 0.001, min: 0, max: 0.01, step: 0.0001, unit: '%' },
    { key: 'minFee', label: '最低手续费', type: 'number', default: 5, min: 0, max: 50, step: 1, unit: '元' },
    { key: 'slippage', label: '滑点', type: 'number', default: 0.001, min: 0, max: 0.01, step: 0.0001 },
    { key: 'type', label: '执行类型', type: 'select', default: 0, options: [
      { label: '立即执行 (市价单)', value: 0 },
      { label: '立即执行 (限价单)', value: 1 }
    ]},
  ],
  example: { commission: 0.0003, stampDuty: 0.001, slippage: 0.001, type: 0 }
}

registerNode(executionNode)
