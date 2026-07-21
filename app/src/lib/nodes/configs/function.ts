/**
 * 指标计算节点（MA/STD/Return/R2/ZScore/VPCorr/ATR）
 */

import { registerNode } from '../registry'
import type { NodeRegistryEntry } from '../types'

/**
 * 每种方法的输入槽位定义
 * slot: 槽位 ID（用于 handle id 和后端参数名）
 * field: 期望的数据字段（close/open/high/low/volume）
 * label: 前端显示的标签
 */
export const functionInputSlots: Record<string, Array<{ slot: string; field: string; label: string }>> = {
  'MA':      [{ slot: 'price',   field: 'close',  label: '价格序列' }],
  'STD':     [{ slot: 'price',   field: 'close',  label: '价格序列' }],
  'R2':      [{ slot: 'price',   field: 'close',  label: '价格序列' }],
  'ZScore':  [{ slot: 'price',   field: 'close',  label: '价格序列' }],
  'Return':  [{ slot: 'price',   field: 'close',  label: '价格序列' }],
  'VPCorr':  [
    { slot: 'price',  field: 'close',  label: '收盘价' },
    { slot: 'volume', field: 'volume', label: '成交量' },
  ],
  'ATR':     [
    { slot: 'high',  field: 'high',  label: '最高价' },
    { slot: 'low',   field: 'low',   label: '最低价' },
    { slot: 'close', field: 'close', label: '收盘价' },
  ],
}

export const basicIndexNode: NodeRegistryEntry = {
  id: 'basic-index',
  label: '指标计算',
  nodeType: 'function',
  category: 'process',
  description: '计算技术指标：移动平均(MA)、标准差(STD)、收益率(Return)、拟合优度(R2)、标准化(ZScore)、量价相关性(VPCorr)、平均真实波幅(ATR)。',
  inputs: ['timeseries'],
  outputs: ['indicator'],
  params: [
    { key: 'method', label: '方法', type: 'select', default: 'MA', options: [
      { label: 'MA (移动平均)', value: 'MA' },
      { label: 'STD (标准差)', value: 'STD' },
      { label: 'Return (收益率)', value: 'Return' },
      { label: 'R2 (拟合优度)', value: 'R2' },
      { label: 'ZScore (标准化)', value: 'ZScore' },
      { label: 'VPCorr (量价相关性)', value: 'VPCorr' },
      { label: 'ATR (平均真实波幅)', value: 'ATR' }
    ]},
    { key: 'range', label: '窗口', type: 'text', default: '5d',
      placeholder: '如 5d、30m、1h（正整数 + s/m/h/d 后缀）',
      pattern: '^\\d+[smhd]$',
      errorMsg: '格式错误，必须为正整数 + s/m/h/d 后缀（如 5d、30m、1h）',
      description: '滑动窗口大小。MA=平滑周期，STD=标准差窗口，Return=回溯步数，R2=拟合窗口，ZScore=标准化窗口，VPCorr=相关系数窗口，ATR=TR均值周期' },
  ],
  example: { method: 'MA', range: '5d' }
}

registerNode(basicIndexNode)
