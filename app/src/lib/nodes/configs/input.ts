/**
 * 数据输入节点
 */

import { registerNode } from '../registry'
import type { NodeRegistryEntry } from '../types'

export const dataSourceNode: NodeRegistryEntry = {
  id: 'data-source',
  label: '数据输入',
  nodeType: 'input',
  category: 'input',
  icon: 'fas fa-database',
  description: '从外部获取行情数据，作为策略的基础输入。支持股票、期货、期权，可配置 OHLCV 字段映射和缺失值处理。',
  inputs: [],
  outputs: ['ohlcv'],
  hasInput: false,
  hasOutput: true,
  params: [
    { key: 'source', label: '来源', type: 'select', default: '股票', options: ['股票', '期货', '期权'] },
    { key: 'code', label: '代码', type: 'text', default: ['sz.000001'] },
    { key: 'close', label: 'close', type: 'text', default: 'close' },
    { key: 'open', label: 'open', type: 'text', default: 'open' },
    { key: 'high', label: 'high', type: 'text', default: 'high' },
    { key: 'low', label: 'low', type: 'text', default: 'low' },
    { key: 'volume', label: 'volume', type: 'text', default: 'volume' },
    { key: 'freq', label: '频率', type: 'select', default: '1d', options: [
      { label: '日线', value: '1d' },
      { label: '周线（日线聚合，周内定稿）', value: '1w' },
      { label: '1 分钟', value: '1m' },
      { label: '5 分钟', value: '5m' },
      { label: '15 分钟', value: '15m' },
      { label: '30 分钟', value: '30m' },
      { label: '1 小时', value: '1h' },
      { label: '4 小时', value: '4h' }
    ], description: '1w 是「周频视图」：基础数据仍为日线，由引擎按 ISO 周聚合成周线，额外输出 close_w / high_w / low_w / open_w / volume_w。采用完成周滞后语义——本周走完后才定稿，下周一才对下游可见，因此周内读到的是上一周的值。挂在它下面的窗口指标（range）自动按周计算，range=20 即 20 周' },
    { key: 'missingHandle', label: '缺失处理', type: 'select', default: 'skip', options: [
      { label: '跳过', value: 'skip' },
      { label: '线性插值', value: 'linear' },
      { label: '前向填充', value: 'forward' },
      { label: '后向填充', value: 'backward' }
    ]},
    // ── 期权输出字段（source='期权' 时显示）──
    // 报价组
    { key: 'bid1', label: '买1价(≈close)', type: 'label', visible: false },
    { key: 'ask1', label: '卖1价(≈close)', type: 'label', visible: false },
    // Greeks 组
    { key: 'theoPrice', label: '理论价格', type: 'label', visible: false },
    { key: 'iv', label: 'IV', type: 'label', visible: false },
    { key: 'delta', label: 'Delta', type: 'label', visible: false },
    { key: 'gamma', label: 'Gamma', type: 'label', visible: false },
    { key: 'theta', label: 'Theta', type: 'label', visible: false },
    { key: 'vega', label: 'Vega', type: 'label', visible: false },
    // 其他组
    { key: 'dte', label: 'DTE', type: 'label', visible: false },
    { key: 'openInterest', label: '未平仓合约数', type: 'label', visible: false },
    { key: 'underlyingPrice', label: '标的价格', type: 'label', visible: false },
    { key: 'optionVolume', label: '交易量', type: 'label', visible: false },
  ],
  example: {
    source: '股票',
    code: ['sz.000001'],
    freq: '1d',
    missingHandle: 'skip'
  }
}

registerNode(dataSourceNode)
