/**
 * 宏观数据输入节点
 * 从 FRED/akshare 获取宏观经济指标数据，作为策略的基础输入
 */

import { registerNode } from '../registry'
import type { NodeRegistryEntry } from '../types'

export const macroInputNode: NodeRegistryEntry = {
  id: 'macro-input',
  label: '宏观数据',
  nodeType: 'macroInput',
  category: 'input',
  icon: 'fas fa-globe',
  description: '从 FRED/akshare 获取宏观经济指标数据（利率、汇率、CPI 等）。国内数据使用前向填充对齐到日频，美国数据使用 T-1 滞后对齐（数据发布延迟）。',
  inputs: [],
  outputs: ['value'],
  hasInput: false,
  hasOutput: true,
  params: [
    {
      key: 'country',
      label: '国家',
      type: 'select',
      default: 'usa',
      options: [
        { label: '中国', value: 'china' },
        { label: '美国', value: 'usa' },
        { label: '全球', value: 'global' }
      ],
      description: '数据所属国家/地区'
    },
    {
      key: 'indicator',
      label: '指标',
      type: 'select',
      default: 'sofr',
      options: [
        // 通用指标
        { label: 'CPI', value: 'cpi' },
        { label: 'PPI', value: 'ppi' },
        { label: 'GDP', value: 'gdp' },
        { label: 'PMI', value: 'pmi' },
        { label: 'M2', value: 'm2' },
        { label: '社融', value: 'social_financing' },
        { label: '失业率', value: 'unemployment' },
        { label: '贸易', value: 'trade' },
        { label: '利率', value: 'interest_rate' },
        { label: '零售销售', value: 'retail_sales' },
        { label: '工业增加值', value: 'industrial_production' },
        { label: '固投', value: 'fixed_asset_investment' },
        { label: '消费者信心', value: 'consumer_confidence' },
        { label: '新屋开工', value: 'housing_starts' },
        { label: '非农就业', value: 'nonfarm' },
        // FRED 指标（仅美国）
        { label: 'SOFR', value: 'sofr' },
        { label: '国债收益率', value: 'treasury_yield' },
        { label: '国债利差', value: 'treasury_spread' },
        { label: '汇率', value: 'fx_rate' }
      ],
      description: '选择宏观经济指标'
    },
    {
      key: 'seriesId',
      label: 'Series ID',
      type: 'text',
      default: '',
      description: '多 series 指标时指定具体 ID（如 DGS10、DEXCHUS），留空使用默认值',
      placeholder: '留空使用默认'
    },
    {
      key: 'fillMethod',
      label: '填充方式',
      type: 'select',
      default: 'forward',
      options: [
        { label: '前向填充', value: 'forward' },
        { label: '线性插值', value: 'linear' },
        { label: '不填充', value: 'none' }
      ],
      description: '低频数据对齐到日频时的填充方式。国内数据默认前向填充，美国数据默认 T-1 滞后'
    }
  ],
  example: {
    country: 'usa',
    indicator: 'sofr',
    seriesId: '',
    fillMethod: 'forward'
  }
}

registerNode(macroInputNode)
