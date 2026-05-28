/**
 * 数据重采样节点
 * 
 * 将高频数据（秒级/分钟级）重采样为低频数据（小时/日级），
 * 支持 OHLCV 标准聚合。适用于多频率策略（如 1min 交易 + 1h 趋势判断）。
 */

import { registerNode } from '../registry'
import type { NodeRegistryEntry } from '../types'

export const resampleNode: NodeRegistryEntry = {
  id: 'resample',
  label: '数据重采样',
  nodeType: 'resample',
  category: 'process',
  description: '将高频数据重采样为低频（如分钟→小时），支持 OHLCV 标准聚合。适用于多频率策略（如 1min 交易 + 1h 趋势判断）。',
  inputs: ['timeseries'],
  outputs: ['resampled'],
  hasInput: true,
  hasOutput: true,
  params: [
    { key: 'target_freq', label: '目标频率', type: 'select', default: '1h', options: [
      { label: '1 分钟', value: '1m' },
      { label: '5 分钟', value: '5m' },
      { label: '15 分钟', value: '15m' },
      { label: '30 分钟', value: '30m' },
      { label: '1 小时', value: '1h' },
      { label: '2 小时', value: '2h' },
      { label: '4 小时', value: '4h' },
      { label: '1 天', value: '1d' }
    ]},
  ],
  example: { target_freq: '1h' }
}

registerNode(resampleNode)
