/**
 * 风控保护节点（止损/止盈/追踪/时间/ATR/MA/R2/MAE）
 */

import { registerNode } from '../registry'
import type { NodeRegistryEntry } from '../types'

export const protectionNode: NodeRegistryEntry = {
  id: 'risk',
  label: '风控保护',
  nodeType: 'protection',
  category: 'risk',
  icon: 'fas fa-shield-alt',
  description: '合并止损、止盈、追踪止损、时间止损和 4 类高级止损（ATR/MA/R²/MAE）。可同时启用，取最先触发的一个。',
  inputs: ['signal'],
  outputs: ['signal'],
  params: [
    // 原有 5 类
    { key: 'stop_loss_enabled', label: '止损开关', type: 'boolean', default: false },
    { key: 'stop_loss_percent', label: '止损比例', type: 'number', default: 0.05, min: 0.01, max: 0.5, step: 0.01, unit: '%' },
    { key: 'take_profit_enabled', label: '止盈开关', type: 'boolean', default: false },
    { key: 'take_profit_percent', label: '止盈比例', type: 'number', default: 0.15, min: 0.01, max: 1.0, step: 0.01, unit: '%' },
    { key: 'trailing_stop_enabled', label: '追踪止损开关', type: 'boolean', default: false },
    { key: 'trailing_stop_percent', label: '追踪止损比例', type: 'number', default: 0.03, min: 0.01, max: 0.3, step: 0.01, unit: '%' },
    { key: 'time_stop_enabled', label: '时间止损开关', type: 'boolean', default: false },
    { key: 'max_bars', label: '最大持仓Bar数', type: 'number', default: 20, min: 1, max: 1000, step: 1, unit: '根' },
    { key: 'formula_stop_enabled', label: '公式止损开关', type: 'boolean', default: false },
    { key: 'formula_stop_expression', label: '公式止损表达式', type: 'textarea', default: '', placeholder: 'close < MA(close, 20)', description: '表达式返回非零值时触发止损，可引用上游节点输出变量' },
    // Phase 0: 4 类新止损
    { key: 'atr_stop_loss_enabled', label: 'ATR止损开关', type: 'boolean', default: false },
    { key: 'atr_period', label: 'ATR周期', type: 'number', default: 20, min: 5, max: 100, step: 1, unit: '根', description: 'ATR 计算窗口，v17 实验推荐 20' },
    { key: 'atr_multiplier', label: 'ATR倍数', type: 'number', default: 2.0, min: 0.5, max: 5.0, step: 0.1, description: '止损距离 = ATR × 倍数，v17 最优 3.12' },
    { key: 'ma_stop_loss_enabled', label: 'MA止损开关', type: 'boolean', default: false },
    { key: 'ma_period', label: 'MA周期', type: 'number', default: 20, min: 5, max: 200, step: 1, unit: '根', description: '收盘价跌破 MA 时触发' },
    { key: 'r2_stop_loss_enabled', label: 'R²止损开关', type: 'boolean', default: false },
    { key: 'r2_period', label: 'R²周期', type: 'number', default: 10, min: 5, max: 50, step: 1, unit: '根', description: 'R² 线性拟合窗口' },
    { key: 'r2_threshold', label: 'R²阈值', type: 'number', default: 0.5, min: 0.1, max: 0.9, step: 0.05, description: 'R² 低于阈值时触发（趋势消失）' },
    { key: 'mae_stop_loss_enabled', label: 'MAE止损开关', type: 'boolean', default: false },
    { key: 'mae_percent', label: 'MAE比例', type: 'number', default: 0.05, min: 0.01, max: 0.3, step: 0.01, unit: '%', description: '最大不利偏移比例（入场价到最低价）' },
  ],
  example: {
    stop_loss_enabled: true, stop_loss_percent: 0.05,
    take_profit_enabled: false,
    trailing_stop_enabled: false,
    time_stop_enabled: false,
    formula_stop_enabled: false, formula_stop_expression: '',
    atr_stop_loss_enabled: false, atr_period: 20, atr_multiplier: 2.0,
    ma_stop_loss_enabled: false, ma_period: 20,
    r2_stop_loss_enabled: false, r2_period: 10, r2_threshold: 0.5,
    mae_stop_loss_enabled: false, mae_percent: 0.05,
  }
}

registerNode(protectionNode)
