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
    { key: 'features', label: '特征列表', type: 'textarea', default: '', placeholder: 'close,ma_5,rsi_14', description: '逗号分隔的特征名，对应上游节点输出' },
    {
      key: 'objective',
      label: '目标函数',
      type: 'select',
      default: 'binary:logistic',
      options: [
        { label: '二分类 (binary:logistic)', value: 'binary:logistic' },
        { label: '多分类 (multi:softprob)', value: 'multi:softprob' },
        { label: '回归 (reg:squarederror)', value: 'reg:squarederror' },
      ],
    },
    { key: 'num_class', label: '分类数', type: 'number', default: 2, min: 2, max: 100, dependsOn: 'objective', dependsValue: 'multi:softprob' },
  ],
  example: { modelFile: '', features: 'close,ma_5,rsi_14', objective: 'binary:logistic', num_class: 2 }
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
