/**
 * CUSUM 累积和变点检测节点
 *
 * 5 种模式：ChangePoint（结构变化）/ Momentum（趋势跟踪）/
 * MeanRevert（均值反转）/ Asset（逐资产）/ Consensus（多资产共识）
 */

import { registerNode } from '../registry'
import type { NodeRegistryEntry } from '../types'

export const cusumNode: NodeRegistryEntry = {
  id: 'cusum',
  label: 'CUSUM 变点检测',
  nodeType: 'cusum',
  category: 'utility',
  description: '累积和变点检测节点。检测收益率序列的结构性变化（regime change），支持趋势跟踪、均值反转、逐资产检测和系统性风险共识模式。输出信号到 DataContext 供 SignalNode 使用。',
  inputs: ['return'],
  outputs: ['signal', 'triggered', 's_pos', 's_neg', 'drift', 'change_points'],
  params: [
    { key: 'mode', label: '检测模式', type: 'select', default: 'changepoint',
      options: [
        { label: 'ChangePoint (变点检测)', value: 'changepoint' },
        { label: 'Momentum (趋势跟踪)', value: 'momentum' },
        { label: 'MeanRevert (均值反转)', value: 'meanrevert' },
        { label: 'Asset (逐资产)', value: 'asset' },
        { label: 'Consensus (多资产共识)', value: 'consensus' },
      ],
      description: 'ChangePoint: 结构变化触发信号; Momentum: S+触发买入S-触发卖出; MeanRevert: 反转信号; Asset: 逐资产独立检测; Consensus: 多资产共识触发系统性信号' },
    { key: 'lambda', label: '容许偏差 λ', type: 'number', default: 0.5, min: 0, max: 2, step: 0.1,
      description: 'k = λ × σ，λ 越小越敏感。推荐 0.3~0.8' },
    { key: 'threshold_multiplier', label: '阈值倍数', type: 'number', default: 4.0, min: 0.5, max: 10, step: 0.5,
      description: 'h = threshold × σ × √N，越大越不容易误触发。推荐 3~5' },
    { key: 'min_obs', label: '最少观测数', type: 'number', default: 30, min: 5, max: 252,
      description: '初期不触发变点的最少交易日，避免初期噪声误判' },
    { key: 'mu', label: '预期均值 μ', type: 'number', default: 0, step: 0.01,
      description: '预期收益率均值，通常取 0 或训练期均值' },
    { key: 'sigma', label: '预期波动率 σ', type: 'number', default: 1, min: 0.01, step: 0.01,
      description: '预期收益率波动率，用于计算 k 和 h 阈值' },
    { key: 'cooldown', label: '冷却天数', type: 'number', default: 0, min: 0, max: 60,
      description: '触发后暂停检测的天数，0 表示不冷却' },
    { key: 'consensus_threshold', label: '共识阈值', type: 'number', default: 2, min: 1, max: 20,
      dependsOn: 'mode', dependsValue: 'consensus',
      description: 'Consensus 模式：最少多少个资产同向触发才输出全局信号' },
  ],
  example: { mode: 'momentum', lambda: 0.5, threshold_multiplier: 4.0, min_obs: 30 }
}

registerNode(cusumNode)
