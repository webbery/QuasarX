/**
 * 指标计算节点（MA/STD/Return/R2/ZScore）
 */

import { registerNode } from '../registry'
import type { NodeRegistryEntry } from '../types'

export const basicIndexNode: NodeRegistryEntry = {
  id: 'basic-index',
  label: '指标计算',
  nodeType: 'function',
  category: 'process',
  description: '计算技术指标：移动平均(MA)、标准差(STD)、收益率(Return)、拟合优度(R2)、标准化(ZScore)。',
  inputs: ['timeseries'],
  outputs: ['indicator'],
  params: [
    { key: 'method', label: '方法', type: 'select', default: 'MA', options: [
      { label: 'MA (移动平均)', value: 'MA' },
      { label: 'STD (标准差)', value: 'STD' },
      { label: 'Return (收益率)', value: 'Return' },
      { label: 'R2 (拟合优度)', value: 'R2' },
      { label: 'ZScore (标准化)', value: 'ZScore' }
    ]},
    { key: 'range', label: '窗口', type: 'text', default: '5d',
      placeholder: '如 5d、30m、1h（正整数 + s/m/h/d 后缀）',
      pattern: '^\\d+[smhd]$',
      errorMsg: '格式错误，必须为正整数 + s/m/h/d 后缀（如 5d、30m、1h）',
      description: '滑动窗口大小，单位：步（数据点数量）。MA=平滑周期，STD=标准差窗口，Return=回溯步数，R2=拟合窗口，ZScore=标准化窗口' },
  ],
  example: { method: 'MA', range: '5d' }
}

registerNode(basicIndexNode)
