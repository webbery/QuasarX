// src/components/flow/config/nodeTypeConfig.ts
// 节点类型配置

export interface NodeTypeConfig {
  type?: string
  color?: string
  icon: string
  hasInput: boolean
  hasOutput: boolean
}

export const NODE_TYPE_CONFIG: Record<string, NodeTypeConfig> = {
  '数据输入': {
    type: 'dataInput',
    color: '#10b981',
    icon: 'fas fa-database',
    hasInput: false,
    hasOutput: true
  },
  '结果输出': {
    type: 'resultOutput',
    color: '#ef4444',
    icon: 'fas fa-chart-bar',
    hasInput: true,
    hasOutput: false
  },
  'default': {
    icon: 'fas fa-cube',
    hasInput: true,
    hasOutput: true
  }
}

export function getNodeIcon(label: string): string {
  return NODE_TYPE_CONFIG[label]?.icon || NODE_TYPE_CONFIG.default.icon
}

export function getNodeColor(label: string): string {
  return NODE_TYPE_CONFIG[label]?.color || '#2962ff'
}

export function getNodeHasInput(label: string): boolean {
  return NODE_TYPE_CONFIG[label]?.hasInput ?? NODE_TYPE_CONFIG.default.hasInput
}

export function getNodeHasOutput(label: string): boolean {
  return NODE_TYPE_CONFIG[label]?.hasOutput ?? NODE_TYPE_CONFIG.default.hasOutput
}
