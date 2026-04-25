/**
 * Chat API 接口封装
 *
 * 对接现有的 agent.ts 和 rag.ts
 */

import { sendAgentMessage, getAgentConfig } from '@/lib/agent'
import { ragQuery } from '@/ts/rag'

export interface AskOptions {
  context?: string      // 背景知识上下文（如行情数据）
  onChunk?: (text: string) => void
  onDone?: (fullText: string) => void
  onError?: (error: Error) => void
}

/**
 * 向 AI 助手提问
 *
 * @param question 用户问题
 * @param options 可选配置
 * @returns AI 回复
 */
export async function askAI(
  question: string,
  options: AskOptions = {}
): Promise<string> {
  const { context, onChunk, onDone, onError } = options

  // 检查 Agent 配置
  const agentConfig = getAgentConfig()
  if (!agentConfig) {
    return '请先在设置中配置 AI 服务。打开 设置 > Agent 大语言模型配置，填写服务 URL、协议和 API Key。'
  }

  try {
    // 构建系统 Prompt
    let systemPrompt = '你是 QuasarX 量化交易助手的 AI 助手。请提供专业、简洁的回答。'

    // 注入背景知识上下文
    if (context) {
      systemPrompt += `\n\n${context}`
    }

    // 调用 RAG + LLM
    const response = await ragQuery(question,
      async (prompt: string) => {
        // LLM 回调
        const result = await sendAgentMessage(prompt)
        if (onChunk) {
          onChunk(result)
        }
        return result
      },
      5,  // topK
      systemPrompt
    )

    if (onDone) {
      onDone(response.answer)
    }

    return response.answer
  } catch (error) {
    console.error('[Chat] AI 请求失败:', error)

    if (onError) {
      onError(error as Error)
    }

    // 返回友好错误提示
    return `抱歉，AI 服务暂时不可用。错误详情: ${(error as Error).message}`
  }
}

/**
 * 快捷提问 - 行情分析
 */
export async function askMarketAnalysis(): Promise<string> {
  return askAI('请分析当前市场行情，包括：\n1. 主要指数走势\n2. 板块涨跌情况\n3. 市场情绪指标')
}

/**
 * 快捷提问 - 策略建议
 */
export async function askStrategyAdvice(strategyName?: string): Promise<string> {
  const question = strategyName
    ? `请为策略"${strategyName}"提供优化建议`
    : '请提供一些策略设计的基本原则和建议'

  return askAI(question)
}

/**
 * 快捷提问 - 风险预警
 */
export async function askRiskWarning(): Promise<string> {
  return askAI('请分析我当前的风险状况，并提供风险管理建议')
}

/**
 * 快捷提问 - 持仓诊断
 */
export async function askPositionDiagnosis(): Promise<string> {
  return askAI('请分析我当前的持仓结构，并提供调仓建议')
}
