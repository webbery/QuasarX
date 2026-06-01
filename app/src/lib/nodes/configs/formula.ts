/**
 * 公式计算节点 — 自定义表达式计算
 */

import { registerNode } from '../registry'
import type { NodeRegistryEntry } from '../types'

export const formulaNode: NodeRegistryEntry = {
  id: 'formula',
  label: '公式计算',
  nodeType: 'formula',
  category: 'process',
  description: '通过自定义公式对输入数据进行计算，输出数值结果。支持四则运算、逻辑运算和截面函数（topk/rank/zscore）。',
  inputs: ['indicator', 'timeseries'],
  outputs: ['value'],
  params: [
    {
      key: 'expression',
      label: '公式表达式',
      type: 'textarea',
      default: 'A * 0.5 + B * 0.3 + C * 0.2',
      placeholder: '如: A * 0.5 + B * 0.3 + C * 0.2',
      description: '公式中的变量名来自上游节点的输出。支持 + - * / () and or not topk() rank() zscore() 等运算符。'
    },
  ],
  example: { expression: 'A * 0.5 + B * 0.3 + C * 0.2' }
}

registerNode(formulaNode)
