/**
 * 行业板块 API 服务
 * 封装 /stocks/sector/* 相关接口：
 * - /quote: 行业板块实时行情
 * - /flow: 板块资金流向信息
 */

import axios from 'axios'

// ============ 板块行情接口 (/stocks/sector/quote) ============

/** 行业板块行情数据项 */
export interface SectorQuote {
  rank: number           // 板块排名
  name: string           // 板块名称
  up_count: number       // 上涨家数
  down_count: number     // 下跌家数
  change_amount: number  // 涨跌额
  change_pct: number     // 涨跌幅（%）
}

/** 行情 API 响应结构 */
export interface SectorQuoteResponse {
  status: string
  date: string
  data: SectorQuote[]
}

// ============ 板块资金流向接口 (/stocks/sector/flow) ============

/** 单个时间点的资金流向数据 */
export interface SectorFlowItem {
  date: string           // 日期
  main: number           // 主力资金
  supbig: number         // 超大单资金
  big: number            // 大单资金
  mid: number            // 中单资金
  small: number          // 小单资金
}

/** 单个板块的资金流向数据 */
export interface SectorFlowData {
  name: string           // 板块名称
  value: SectorFlowItem[]  // 资金流向列表（按日期）
}

/** 资金流向 API 响应结构 */
export interface SectorFlowResponse {
  status: string
  data: SectorFlowData[]
}

/** 资金流向类型 */
export type SectorFlowType = 0 | 1  // 0-当日信息, 1-所有历史信息

// ============ API 调用函数 ============

/**
 * 获取行业板块实时行情
 * @returns 行业板块行情列表
 */
export async function fetchSectorQuotes(): Promise<SectorQuote[]> {
  try {
    const response = await axios.get<SectorQuoteResponse>('/v0/stocks/sector/quote')

    if (response.data.status === 'success' && response.data.data) {
      return response.data.data
    }

    return []
  } catch (error: any) {
    console.error('[fetchSectorQuotes] API 调用失败:', error.message)
    return []
  }
}

/**
 * 获取板块资金流向信息
 * @param type 资金流向类型：0-当日信息，1-所有历史信息
 * @returns 板块资金流向列表
 */
export async function fetchSectorFlow(type: SectorFlowType = 0): Promise<SectorFlowData[]> {
  try {
    // 从 localStorage 获取服务器地址和认证 token
    const server = localStorage.getItem('remote')
    const token = localStorage.getItem('token')

    // 构建请求配置
    const baseURL = server ? `https://${server}` : ''
    const url = `${baseURL}/v0/stocks/sector/flow`

    const config: any = {
      params: { type },
    }

    // 如果有 token，添加认证头
    if (token) {
      config.headers = { 'Authorization': token }
    }

    // 开发环境使用相对路径，生产环境使用完整 URL
    const response = server
      ? await axios.get<SectorFlowResponse>(url, config)
      : await axios.get<SectorFlowResponse>('/v0/stocks/sector/flow', { params: { type } })

    if (response.data.status === 'success' && response.data.data) {
      return response.data.data
    }

    return []
  } catch (error: any) {
    console.error('[fetchSectorFlow] API 调用失败:', error.message)
    return []
  }
}

// ============ 数据处理函数 ============

/**
 * 计算所有板块的总上涨家数和下跌家数
 * @param sectors 行业板块行情列表
 * @returns { totalUp: 总上涨家数, totalDown: 总下跌家数 }
 */
export function calculateTotalAdvanceDecline(sectors: SectorQuote[]): { totalUp: number; totalDown: number } {
  let totalUp = 0
  let totalDown = 0

  for (const sector of sectors) {
    totalUp += sector.up_count
    totalDown += sector.down_count
  }

  return { totalUp, totalDown }
}

/**
 * 计算板块资金流向的总和
 * @param flowItem 单个时间点的资金流向数据
 * @returns 总资金流（主力+超大单+大单+中单+小单）
 */
export function calculateTotalFlow(flowItem: SectorFlowItem): number {
  return flowItem.main + flowItem.supbig + flowItem.big + flowItem.mid + flowItem.small
}

/**
 * 获取资金流入 Top N 板块
 * @param flows 板块资金流向列表
 * @param topN 返回前 N 个
 * @returns 资金流入最多的板块列表
 */
export function getTopInflowSectors(flows: SectorFlowData[], topN: number = 5): SectorFlowData[] {
  return [...flows]
    .sort((a, b) => {
      const totalA = a.value.length > 0 ? calculateTotalFlow(a.value[0]) : 0
      const totalB = b.value.length > 0 ? calculateTotalFlow(b.value[0]) : 0
      return totalB - totalA
    })
    .slice(0, topN)
}

/**
 * 获取资金流出 Top N 板块
 * @param flows 板块资金流向列表
 * @param topN 返回前 N 个
 * @returns 资金流出最多的板块列表
 */
export function getTopOutflowSectors(flows: SectorFlowData[], topN: number = 5): SectorFlowData[] {
  return [...flows]
    .sort((a, b) => {
      const totalA = a.value.length > 0 ? calculateTotalFlow(a.value[0]) : 0
      const totalB = b.value.length > 0 ? calculateTotalFlow(b.value[0]) : 0
      return totalA - totalB
    })
    .slice(0, topN)
}
