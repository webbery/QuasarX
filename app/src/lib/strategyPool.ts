/**
 * 策略标的池解析工具
 * 从策略流程图中提取标的池信息
 */

export interface Security {
  code: string
  name: string
}

export interface FlowNode {
  id: string
  type: string
  data: {
    label: string
    nodeType: string
    params?: Record<string, {
      value: any
      type: string
      options?: any[]
    }>
  }
  position: {
    x: number
    y: number
  }
}

export interface FlowEdge {
  id: string
  source: string
  target: string
  sourceHandle?: string
  targetHandle?: string
}

export interface FlowData {
  nodes: FlowNode[]
  edges: FlowEdge[]
}

/**
 * 从策略流程图中提取标的池
 * @param flowData 流程图数据
 * @returns 标的池列表
 */
export function extractSecuritiesFromFlowData(flowData: FlowData): Security[] {
  if (!flowData || !flowData.nodes) {
    return []
  }

  const securities = new Map<string, Security>()

  for (const node of flowData.nodes) {
    const nodeType = node.data?.nodeType
    const params = node.data?.params

    if (!params) continue

    // 1. 从行情输入节点 (QuoteNode/InputNode) 提取
    if (nodeType === 'input' || nodeType === 'quote') {
      extractFromInputNode(params, securities)
    }

    // 2. 从信号生成节点 (SignalNode) 提取
    if (nodeType === 'signal') {
      extractFromSignalNode(params, securities)
    }

    // 3. 从投资组合节点 (PortfolioNode) 提取
    if (nodeType === 'portfolio') {
      extractFromPortfolioNode(params, securities)
    }
  }

  return Array.from(securities.values())
}

/**
 * 从输入节点提取标的
 */
function extractFromInputNode(
  params: Record<string, { value: any; type: string }>,
  securities: Map<string, Security>
): void {
  // 检查是否有"代码"参数
  const codeParam = params['代码'] || params['code']
  if (codeParam && codeParam.value) {
    const codes = Array.isArray(codeParam.value)
      ? codeParam.value
      : [codeParam.value]

    for (const code of codes) {
      if (code && typeof code === 'string' && code.trim()) {
        // 检查是否是"全股票"标记
        if (code === 'all' || code === '全部' || code === '全市场') {
          securities.set('all_stocks', {
            code: 'all_stocks',
            name: '全市场股票'
          })
          return
        }

        const security = parseSecurityCode(code)
        if (security) {
          securities.set(security.code, security)
        }
      }
    }
  }
}

/**
 * 从信号节点提取标的
 */
function extractFromSignalNode(
  params: Record<string, { value: any; type: string }>,
  securities: Map<string, Security>
): void {
  // 检查是否有"代码"参数
  const codeParam = params['代码'] || params['code']
  if (codeParam && codeParam.value) {
    const codes = Array.isArray(codeParam.value)
      ? codeParam.value
      : [codeParam.value]

    for (const code of codes) {
      if (code && typeof code === 'string' && code.trim()) {
        // 检查是否是"全股票"标记
        if (code === 'all' || code === '全部' || code === '全市场') {
          securities.set('all_stocks', {
            code: 'all_stocks',
            name: '全市场股票'
          })
          return
        }

        const security = parseSecurityCode(code)
        if (security) {
          securities.set(security.code, security)
        }
      }
    }
  }
}

/**
 * 从投资组合节点提取标的
 */
function extractFromPortfolioNode(
  params: Record<string, { value: any; type: string }>,
  securities: Map<string, Security>
): void {
  // 检查是否有"pool"或"交易池"参数
  const poolParam = params['pool'] || params['交易池']
  if (poolParam && poolParam.value) {
    const codes = Array.isArray(poolParam.value)
      ? poolParam.value
      : [poolParam.value]

    for (const code of codes) {
      if (code && typeof code === 'string' && code.trim()) {
        // 检查是否是"全股票"标记
        if (code === 'all' || code === '全部' || code === '全市场') {
          securities.set('all_stocks', {
            code: 'all_stocks',
            name: '全市场股票'
          })
          return
        }

        const security = parseSecurityCode(code)
        if (security) {
          securities.set(security.code, security)
        }
      }
    }
  }
}

/**
 * 解析证券代码，标准化格式
 * @param code 证券代码（可能是 sz.000001, 000001.SZ, 600519.SH 等格式）
 * @returns 标准化后的证券信息
 */
function parseSecurityCode(code: string): Security | null {
  if (!code || typeof code !== 'string') {
    return null
  }

  code = code.trim()
  if (!code) {
    return null
  }

  // 尝试解析不同格式的代码
  // 格式 1: sz.000001, sh.600519
  const match1 = code.match(/^(sz|sh)\.(\d+)$/i)
  if (match1) {
    const exchange = match1[1].toLowerCase() === 'sz' ? 'SZ' : 'SH'
    const symbol = match1[2]
    return {
      code: `${symbol}.${exchange}`,
      name: ''
    }
  }

  // 格式 2: 000001.SZ, 600519.SH
  const match2 = code.match(/^(\d+)\.(SZ|SH|SZSE|SSE)$/i)
  if (match2) {
    const symbol = match2[1]
    const exchange = match2[2].toUpperCase()
    return {
      code: `${symbol}.${exchange === 'SZSE' ? 'SZ' : exchange === 'SSE' ? 'SH' : exchange}`,
      name: ''
    }
  }

  // 格式 3: 纯数字代码（需要用户补充交易所信息）
  if (/^\d+$/.test(code)) {
    return {
      code: code,
      name: ''
    }
  }

  // 其他格式，直接返回
  return {
    code: code,
    name: ''
  }
}

/**
 * 检查标的池是否包含"全市场股票"
 * @param securities 标的池列表
 * @returns 是否是全市场
 */
export function isAllStocks(securities: Security[]): boolean {
  return securities.some(s => s.code === 'all_stocks')
}

/**
 * 格式化标的池显示文本
 * @param securities 标的池列表
 * @param maxLength 最大显示数量
 * @returns 格式化后的文本
 */
export function formatPoolDisplayText(
  securities: Security[],
  maxLength: number = 5
): string {
  if (!securities || securities.length === 0) {
    return '暂无标的'
  }

  // 检查是否是全市场
  if (isAllStocks(securities)) {
    return '全市场股票'
  }

  if (securities.length <= maxLength) {
    return securities
      .map(s => s.name ? `${s.code}(${s.name})` : s.code)
      .join(', ')
  }

  const shown = securities.slice(0, maxLength)
  const remaining = securities.length - maxLength
  return (
    shown
      .map(s => s.name ? `${s.code}(${s.name})` : s.code)
      .join(', ') + `... 等${remaining}只`
  )
}
