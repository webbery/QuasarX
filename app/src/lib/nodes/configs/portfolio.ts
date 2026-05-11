/**
 * 投资组合节点
 */

import { registerNode } from '../registry'
import type { NodeRegistryEntry } from '../types'

export const portfolioNode: NodeRegistryEntry = {
  id: 'portfolio',
  label: '投资组合',
  nodeType: 'portfolio',
  category: 'execution',
  description: '管理多标的投资组合，可配置仓位比例和约束条件。',
  inputs: ['signal'],
  outputs: ['signal'],
  params: [
    { key: 'configId', label: '配置 ID', type: 'config-select', default: '', placeholder: '选择或创建配置', options: [] },
    { key: 'configDesc', label: '配置简介', type: 'textarea', default: '', placeholder: '描述策略投资目标、风险偏好等' },
    { key: 'positionRatio', label: '仓位比例', type: 'number', default: 1.0, min: 0, max: 1, step: 0.1 },
  ],
  example: { positionRatio: 1.0 }
}

registerNode(portfolioNode)
