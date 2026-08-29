/**
 * 工具节点（调试 / 测试）
 */

import { registerNode } from '../registry'
import type { NodeRegistryEntry } from '../types'

export const debugNode: NodeRegistryEntry = {
  id: 'debug',
  label: '调试',
  nodeType: 'debug',
  category: 'utility',
  icon: 'fas fa-bug',
  description: '将流经此节点的数据导出为 CSV 文件，用于策略调试。',
  inputs: ['timeseries'],
  outputs: ['timeseries'],
  params: [
    { key: 'suffix', label: '输出格式', type: 'select', default: 'csv',
      options: [
        { label: 'CSV', value: 'csv' },
      ],
      description: '导出文件格式' },
    { key: 'outputFields', label: '导出字段', type: 'multiselect', default: [],
      options: [],
      description: '勾选需要导出的数据字段，来源于上游节点输出' },
    { key: 'visualize', label: '可视化', type: 'button', default: 'visualize',
      description: '切换到可视化分析面板查看数据' },
  ],
  example: {}
}

registerNode(debugNode)

export const testNode: NodeRegistryEntry = {
  id: 'test',
  label: '测试',
  nodeType: 'test',
  category: 'utility',
  icon: 'fas fa-vial',
  description: '测试节点，用于验证策略流程。',
  inputs: ['timeseries'],
  outputs: ['timeseries'],
  params: [
    { key: 'param', label: '参数', type: 'text', default: '' },
  ],
  example: { param: '' }
}

registerNode(testNode)
