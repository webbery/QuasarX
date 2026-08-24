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
  'Median':  [{ slot: 'price',   field: 'close',  label: '价格序列' }],
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

const INPUT_HANDLE_PREFIX = 'input-'

/**
 * 统一 function 节点输入 handle id 规则
 * - 单输入方法（MA/STD/R2/ZScore/Return）→ "input-{firstSlot.slot}"，如 "input-price"
 * - 多输入方法（VPCorr/ATR）→ 同样的命名格式
 * - 未识别方法兜底为 "input-price"（MA 槽位）
 *
 * 渲染（StrategyNode / StrategyNodeHandles）、导入（useStrategyImportExport）、
 * 校验（useFlowOperations 的 input- 前缀检查）共用此函数，避免历史 bug 再次分叉。
 */
export const getFunctionInputHandleId = (method: string): string => {
  const slots = functionInputSlots[method]
  if (slots && slots.length > 0) {
    return `${INPUT_HANDLE_PREFIX}${slots[0].slot}`
  }
  return `${INPUT_HANDLE_PREFIX}${(functionInputSlots['MA']?.[0]?.slot) || 'price'}`
}

/**
 * 获取指定方法全部输入 handle id（多输入节点用）
 */
export const getFunctionInputHandleIds = (method: string): string[] => {
  const slots = functionInputSlots[method] || []
  return slots.map(s => `${INPUT_HANDLE_PREFIX}${s.slot}`)
}

/**
 * 任意槽位 → handle id。多输入节点每个 slot 渲染 handle 时调用，与单输入规则共享同一前缀常量。
 */
export const getSlotInputHandleId = (slot: { slot: string }): string => {
  return `${INPUT_HANDLE_PREFIX}${slot.slot}`
}

export const basicIndexNode: NodeRegistryEntry = {
  id: 'basic-index',
  label: '指标计算',
  nodeType: 'function',
  category: 'process',
  icon: 'fas fa-calculator',
  description: '计算技术指标：移动平均(MA)、标准差(STD)、收益率(Return)、拟合优度(R2)、标准化(ZScore)、中位数(Median)、量价相关性(VPCorr)、平均真实波幅(ATR)。',
  inputs: ['timeseries'],
  outputs: ['indicator'],
  params: [
    { key: 'method', label: '方法', type: 'select', default: 'MA', options: [
      { label: 'MA (移动平均)', value: 'MA' },
      { label: 'STD (标准差)', value: 'STD' },
      { label: 'Return (收益率)', value: 'Return' },
      { label: 'R2 (拟合优度)', value: 'R2' },
      { label: 'ZScore (标准化)', value: 'ZScore' },
      { label: 'Median (中位数)', value: 'Median' },
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
