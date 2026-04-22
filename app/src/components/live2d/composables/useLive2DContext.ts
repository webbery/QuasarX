/**
 * Live2D 情境上下文
 * 
 * 根据当前视图提供相关数据给 AI 助手
 */

import { computed, type Ref } from 'vue'

export interface Live2DContext {
  currentView: string
  positions?: any[]
  orders?: any[]
  riskData?: any
  marketData?: any
  strategyInfo?: any
}

/**
 * 创建情境上下文
 * 
 * @param currentViewRef 当前视图的 ref
 * @returns 情境数据
 */
export function useLive2DContext(currentViewRef: Ref<string>) {
  const context = computed<Live2DContext>(() => {
    const baseContext: Live2DContext = {
      currentView: currentViewRef.value,
    }

    // 根据视图注入数据
    switch (currentViewRef.value) {
      case 'position':
        // 从 localStorage 或其他 store 获取持仓数据
        baseContext.positions = getPositionsFromStorage()
        break
      case 'risk':
        baseContext.riskData = getRiskDataFromStorage()
        break
      case 'strategy':
        baseContext.strategyInfo = getStrategyInfoFromStorage()
        break
      case 'account':
        baseContext.marketData = getMarketDataFromStorage()
        break
    }

    return baseContext
  })

  return { context }
}

/**
 * 生成情境感知提示
 */
export function getContextualPrompt(context: Live2DContext): string {
  const { currentView, positions, riskData, strategyInfo } = context

  switch (currentView) {
    case 'account':
      return '用户正在查看账户总览，可以提供市场行情和账户概况分析。'
    
    case 'strategy':
      if (strategyInfo) {
        return `用户正在设计策略"${strategyInfo.name}"。可以提供策略优化建议。`
      }
      return '用户正在策略工厂，可以提供策略设计建议。'
    
    case 'risk':
      if (riskData) {
        return `用户正在查看风险控制。当前风险评分: ${riskData.score || '未知'}。可以提供风险管理建议。`
      }
      return '用户正在查看风险控制，可以提供风险管理建议。'
    
    case 'position':
      if (positions && positions.length > 0) {
        return `用户当前有 ${positions.length} 个持仓。可以提供仓位管理和调仓建议。`
      }
      return '用户正在查看持仓管理，当前无持仓。可以提供建仓建议。'
    
    case 'datacenter':
      return '用户正在数据中心，可以提供数据查询服务。'
    
    case 'knowledge_base':
      return '用户正在查看知识库，可以回答相关问题。'
    
    default:
      return '我是 QuasarX AI 助手，可以回答交易、策略、风控相关问题。'
  }
}

// 辅助函数：从 localStorage 获取数据
function getPositionsFromStorage() {
  try {
    const data = localStorage.getItem('quasarx_positions')
    return data ? JSON.parse(data) : null
  } catch {
    return null
  }
}

function getRiskDataFromStorage() {
  try {
    const data = localStorage.getItem('quasarx_risk')
    return data ? JSON.parse(data) : null
  } catch {
    return null
  }
}

function getStrategyInfoFromStorage() {
  try {
    const data = localStorage.getItem('quasarx_current_strategy')
    return data ? JSON.parse(data) : null
  } catch {
    return null
  }
}

function getMarketDataFromStorage() {
  try {
    const data = localStorage.getItem('quasarx_market')
    return data ? JSON.parse(data) : null
  } catch {
    return null
  }
}
