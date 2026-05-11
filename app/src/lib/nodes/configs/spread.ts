/**
 * 价差计算节点
 */

import { registerNode } from '../registry'
import type { NodeRegistryEntry } from '../types'

export const spreadNode: NodeRegistryEntry = {
  id: 'spread',
  label: '价差计算',
  nodeType: 'spread',
  category: 'process',
  description: '计算两个标的之间的价差，支持简单价差(A-B)、对数价差(ln(A)-ln(B))和滚动回归(A-β×B)。适用于配对交易。',
  inputs: ['timeseries', 'timeseries'],
  outputs: ['spread'],
  hasInput: true,
  hasOutput: true,
  params: [
    { key: 'stockA', label: '标的A', type: 'text', default: 'sz.600000', placeholder: '输入标的A的代码（如 sz.600000）' },
    { key: 'stockB', label: '标的B', type: 'text', default: 'sz.600036', placeholder: '输入标的B的代码（如 sz.600036）' },
    { key: 'method', label: '方法', type: 'select', default: 'rolling_regression', options: [
      { label: '简单价差 (A - B)', value: 'simple_diff' },
      { label: '对数价差 (ln(A) - ln(B))', value: 'log_diff' },
      { label: '滚动回归 (A - β×B)', value: 'rolling_regression' }
    ]},
    { key: 'window', label: '窗口大小', type: 'number', default: 60, min: 10, max: 500, step: 10, unit: '天' },
    { key: 'beta', label: '对冲比例β', type: 'number', default: 1.0, min: 0.1, max: 10.0, step: 0.1 },
  ],
  example: { stockA: 'sz.600000', stockB: 'sz.600036', method: 'rolling_regression', window: 60, beta: 1.0 }
}

registerNode(spreadNode)
