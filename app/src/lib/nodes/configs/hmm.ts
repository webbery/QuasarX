/**
 * HMM 隐马尔可夫节点（因果推理 category）
 * 用于推断市场状态（牛/熊/震荡...）。
 * 离线批量训练，diag 协方差，状态数用户自定义。
 */

import { registerNode } from '../registry'
import type { NodeRegistryEntry } from '../types'

export const hmmNode: NodeRegistryEntry = {
  id: 'hmm',
  label: 'HMM 市场状态识别',
  nodeType: 'hmm',
  category: 'causal',
  description: '隐马尔可夫模型，用于推断市场状态（牛/熊/震荡...）。离线批量训练，diag 协方差。输出状态编号、概率分布、转移矩阵和期望持续时间。',
  inputs: ['timeseries'],
  outputs: ['hmm_state', 'hmm_probs', 'hmm_transition', 'hmm_duration'],
  params: [
    { key: 'n_states', label: '状态数', type: 'number', default: 3, min: 2, max: 6,
      description: '隐状态数量，2-6 之间' },
    { key: 'features', label: '输入特征', type: 'text', default: 'close',
      placeholder: '逗号分隔的特征名，如 close,volatility,rsi' },
    { key: 'train_window', label: '训练窗口(天)', type: 'number', default: 252,
      description: '每次训练使用的历史天数' },
    { key: 'retrain_interval', label: '重训间隔(天)', type: 'number', default: 60,
      description: '每隔多少天重新训练模型' },
    { key: 'warmup_period', label: '预热期(天)', type: 'number', default: 60,
      description: '跳过前N天数据，不输出状态' },
    { key: 'max_iter', label: 'EM最大迭代', type: 'number', default: 100,
      description: 'EM 算法最大迭代次数' },
    { key: 'tol', label: 'EM收敛阈值', type: 'number', default: 1e-4, step: 'any',
      description: '对数似然变化小于此值时收敛' },
    { key: 'regularization', label: '正则化ε', type: 'number', default: 1e-6, step: 'any',
      description: '协方差矩阵正则化参数，防止退化' },
    { key: 'random_seed', label: '随机种子', type: 'number', default: 42,
      description: '用于参数初始化，保证可复现' },
    { key: 'state_labels', label: '状态标签', type: 'text', default: 'state0,state1,state2',
      placeholder: '逗号分隔的状态名称，仅用于展示' },
  ],
  example: { n_states: 3, features: 'close,volatility' }
}

registerNode(hmmNode)
