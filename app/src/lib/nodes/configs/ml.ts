/**
 * 机器学习模型节点（XGBoost / CNN）
 */

import { registerNode } from '../registry'
import type { NodeRegistryEntry } from '../types'

export const xgboostNode: NodeRegistryEntry = {
  id: 'xgboost',
  label: 'XGBoost',
  nodeType: 'xgboost',
  category: 'ml',
  description: '使用 XGBoost 模型进行预测。需要上传预训练好的模型文件。',
  inputs: ['features'],
  outputs: ['prediction'],
  params: [
    { key: 'modelFile', label: '上传模型', type: 'file', default: '' },
  ],
  example: { modelFile: '' }
}

registerNode(xgboostNode)

export const cnnNode: NodeRegistryEntry = {
  id: 'cnn',
  label: 'CNN 模型',
  nodeType: 'backtest',
  category: 'ml',
  description: '使用 CNN（卷积神经网络）模型进行预测。',
  inputs: ['features'],
  outputs: ['prediction'],
  params: [],
  example: {}
}

registerNode(cnnNode)
