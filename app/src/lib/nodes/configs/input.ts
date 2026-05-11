/**
 * 数据输入节点
 */

import { registerNode } from '../registry'
import type { NodeRegistryEntry } from '../types'

export const dataSourceNode: NodeRegistryEntry = {
  id: 'data-source',
  label: '数据输入',
  nodeType: 'input',
  category: 'input',
  description: '从外部获取行情数据，作为策略的基础输入。支持股票、期货，可配置 OHLCV 字段映射和缺失值处理。',
  inputs: [],
  outputs: ['ohlcv'],
  hasInput: false,
  hasOutput: true,
  params: [
    { key: 'source', label: '来源', type: 'select', default: '股票', options: ['股票', '期货'] },
    { key: 'code', label: '代码', type: 'text', default: ['sz.000001'] },
    { key: 'close', label: 'close', type: 'text', default: 'close' },
    { key: 'open', label: 'open', type: 'text', default: 'open' },
    { key: 'high', label: 'high', type: 'text', default: 'high' },
    { key: 'low', label: 'low', type: 'text', default: 'low' },
    { key: 'volume', label: 'volume', type: 'text', default: 'volume' },
    { key: 'freq', label: '频率', type: 'select', default: '1d', options: ['1d', '1m', '5m', '15m', '30m', '1h', '4h', '1w'] },
    { key: 'missingHandle', label: '缺失处理', type: 'select', default: 'skip', options: [
      { label: '跳过', value: 'skip' },
      { label: '线性插值', value: 'linear' },
      { label: '前向填充', value: 'forward' },
      { label: '后向填充', value: 'backward' }
    ]},
  ],
  example: {
    source: '股票',
    code: ['sz.000001'],
    freq: '1d',
    missingHandle: 'skip'
  }
}

registerNode(dataSourceNode)
