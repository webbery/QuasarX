import { registerNode } from '../registry'
import type { NodeRegistryEntry } from '../types'

export const emdNode: NodeRegistryEntry = {
  id: 'emd',
  label: 'EMD 分解',
  nodeType: 'emd',
  category: 'process',
  description: '经验模态分解（Empirical Mode Decomposition），将非平稳信号分解为多个本征模态函数（IMF），适用于趋势提取和降噪。',
  inputs: ['timeseries'],
  outputs: ['imf'],
  params: [
    { key: 'numIMFs', label: 'IMF 数量', type: 'number', default: 5, min: 1, max: 20,
      description: '分解的 IMF 分量数量，一般 3~8 个即可' },
  ],
  example: { numIMFs: 5 }
}

registerNode(emdNode)
