import { registerNode } from '../registry'
import type { NodeRegistryEntry } from '../types'

export const emdNode: NodeRegistryEntry = {
  id: 'emd',
  label: 'EMD 分解',
  nodeType: 'emd',
  category: 'process',
  description: '经验模态分解，将非平稳信号分解为多个本征模态函数（IMF）。支持 EMD、CEEMDAN 和 VMD 三种算法。',
  inputs: ['timeseries'],
  outputs: ['imf'],
  params: [
    { key: 'method', label: '算法', type: 'select', default: 'emd',
      options: [
        { label: 'EMD (标准)', value: 'emd' },
        { label: 'CEEMDAN (完备集合)', value: 'ceemdan' },
        { label: 'VMD (变分)', value: 'vmd' },
      ],
      description: 'EMD 快速；CEEMDAN 抑制模态混叠；VMD 数学严格+中心频率可解释' },
    { key: 'numIMFs', label: 'IMF 数量', type: 'number', default: 5, min: 1, max: 20,
      description: '分解的 IMF 分量数量，一般 3~8 个即可' },
    { key: 'ensembles', label: '集合数', type: 'number', default: 50, min: 10, max: 200,
      dependsOn: 'method', dependsValue: 'ceemdan',
      description: 'CEEMDAN 噪声样本数量，越大越精确但越慢' },
    { key: 'noiseStd', label: '噪声强度', type: 'number', default: 0.2, min: 0.01, max: 1.0, step: 0.01,
      dependsOn: 'method', dependsValue: 'ceemdan',
      description: 'CEEMDAN 噪声标准差（相对信号 std 的比例）' },
    { key: 'alpha', label: '带宽惩罚 α', type: 'number', default: 2000, min: 100, max: 10000,
      dependsOn: 'method', dependsValue: 'vmd',
      description: 'VMD 带宽惩罚参数：大=窄带，小=宽带' },
    { key: 'tau', label: '对偶步长 τ', type: 'number', default: 0, min: 0, max: 1, step: 0.1,
      dependsOn: 'method', dependsValue: 'vmd',
      description: 'VMD 对偶上升步长：0=严格重构，大=容忍噪声' },
    { key: 'tol', label: '收敛阈值', type: 'number', default: 0.000001, min: 1e-9, max: 1e-3,
      dependsOn: 'method', dependsValue: 'vmd',
      description: 'VMD 收敛阈值：越小越精确但迭代越多' },
  ],
  example: { method: 'emd', numIMFs: 5 }
}

registerNode(emdNode)
