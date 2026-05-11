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
  description: '将流经此节点的数据导出为 CSV 文件，用于策略调试。',
  inputs: ['timeseries'],
  outputs: ['timeseries'],
  params: [
    { key: 'downloadPath', label: '下载路径', type: 'directory', default: '' },
    { key: 'downloadFile', label: '下载文件', type: 'download', default: '' },
  ],
  example: { downloadPath: '' }
}

registerNode(debugNode)

export const testNode: NodeRegistryEntry = {
  id: 'test',
  label: '测试',
  nodeType: 'test',
  category: 'utility',
  description: '测试节点，用于验证策略流程。',
  inputs: ['timeseries'],
  outputs: ['timeseries'],
  params: [
    { key: 'param', label: '参数', type: 'text', default: '' },
  ],
  example: { param: '' }
}

registerNode(testNode)
