/**
 * 因子合成节点 — 多因子加权合成综合得分
 */

import { registerNode } from '../registry'
import type { NodeRegistryEntry } from '../types'

export const factorCombineNode: NodeRegistryEntry = {
  id: 'factor-combine',
  label: '因子合成',
  nodeType: 'factor_combine',
  category: 'process',
  description: '将多个因子加权合成为综合得分（composite_score）。支持等权平均和自定义权重。输出供 SignalNode 使用。',
  inputs: ['indicator', 'timeseries'],
  outputs: ['composite_score'],
  params: [
    {
      key: 'method',
      label: '合成方法',
      type: 'select',
      default: 'equal',
      options: [
        { label: '等权平均', value: 'equal' },
        { label: '自定义权重', value: 'custom' },
      ],
      description: '等权平均：所有因子等权重合成；自定义权重：按输入顺序指定各因子权重'
    },
    {
      key: 'weights',
      label: '权重',
      type: 'text',
      default: '',
      placeholder: '如: 0.3,0.5,0.2（逗号分隔，与输入顺序对应）',
      dependsOn: 'method',
      dependsValue: 'custom',
      description: '权重按输入连接顺序对应各因子，总和不必为1（会自动归一化）。NaN 因子跳过后用剩余权重归一化。'
    },
  ],
  example: { method: 'equal' }
}

registerNode(factorCombineNode)
