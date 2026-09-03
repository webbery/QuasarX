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
  description: '导出/导入 CSV：export 模式将流经节点的数据写为 CSV；import 模式从 CSV 读取数据注入到策略图。',
  inputs: ['timeseries'],
  outputs: ['timeseries'],
  params: [
    { key: 'mode', label: '模式', type: 'select', default: 'export',
      options: [
        { label: '导出 (export)', value: 'export' },
        { label: '导入 (import)', value: 'import' },
      ],
      description: 'export=导出上游数据为 CSV；import=从 CSV 读取数据注入到策略图' },
    { key: 'file_path', label: 'CSV 路径', type: 'text', default: '',
      description: 'import 模式必填，CSV 绝对路径。格式: datetime,symbol1.feature1,symbol1.feature2,...' },
    { key: 'suffix', label: '输出格式', type: 'select', default: 'csv',
      options: [
        { label: 'CSV', value: 'csv' },
      ],
      description: 'export 模式的导出文件格式' },
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
