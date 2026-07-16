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
  description: '管理多标的投资组合，可配置仓位比例、sizing 方法和约束条件。',
  inputs: ['signal'],
  outputs: ['signal'],
  params: [
    { key: 'configId', label: '配置 ID', type: 'config-select', default: '', placeholder: '选择或创建配置', options: [] },
    { key: 'configDesc', label: '配置简介', type: 'textarea', default: '', placeholder: '描述策略投资目标、风险偏好等' },
    { key: 'positionRatio', label: '仓位比例', type: 'number', default: 1.0, min: 0, max: 1, step: 0.1 },
    {
      key: 'sizing_method',
      label: '仓位计算方法',
      type: 'select',
      default: 'equal',
      options: [
        { label: '等权分配', value: 'equal' },
        { label: 'Kelly 公式', value: 'kelly' },
        { label: '目标波动率', value: 'volatility_target' },
        { label: '信号强度加权', value: 'strength' },
      ],
    },
    { key: 'max_single_pct', label: '单标的上限', type: 'number', default: 1.0, min: 0, max: 1, step: 0.1 },
    { key: 'max_total_pct', label: '总仓位上限', type: 'number', default: 1.0, min: 0, max: 1, step: 0.1 },
    { key: 'volatility_target', label: '波动率目标', type: 'number', default: 0.02, min: 0, max: 1, step: 0.01, dependsOn: 'sizing_method', dependsValue: 'volatility_target' },
  ],
  example: { positionRatio: 1.0, sizing_method: 'equal' }
}

registerNode(portfolioNode)
