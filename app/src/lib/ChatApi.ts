/**
 * Chat API 接口封装
 *
 * 整合 Skill 系统、RAG 知识库、多轮对话和对话摘要压缩
 */

import { sendAgentMessages, getAgentConfig } from '@/lib/agent'
import { routeAndExecute, registerSkill } from '@/lib/skillRouter'
import { defaultKnowledgeSkill } from '@/skills/defaultKnowledgeSkill'
import { buildSystemPrompt } from '@/ts/rag'
import type { ChatMessage } from '@/stores/chatStore'
import type { SkillContext, SkillResult } from '@/types/skill'

// 注册默认 Skill
registerSkill(defaultKnowledgeSkill)

export interface AskOptions {
  context?: string              // 背景知识上下文（如行情数据）
  history?: ChatMessage[]       // 对话历史
  onChunk?: (text: string) => void
  onDone?: (fullText: string) => void
  onError?: (error: Error) => void
}

/**
 * 向 AI 助手提问（核心方法）
 * 
 * 完整流程：
 * 1. Skill 路由匹配
 * 2. 对话历史压缩（如果过长）
 * 3. 构建完整消息（系统提示词 + Skill 结果 + 对话历史 + 用户问题）
 * 4. 调用 LLM
 *
 * @param question 用户问题
 * @param options 可选配置
 * @returns AI 回复
 */
export async function askAI(
  question: string,
  options: AskOptions = {}
): Promise<string> {
  const { context, history, onChunk, onDone, onError } = options

  // 检查 Agent 配置
  const agentConfig = getAgentConfig()
  if (!agentConfig) {
    return '请先在设置中配置 AI 服务。打开 设置 > Agent 大语言模型配置，填写服务 URL、协议和 API Key。'
  }

  try {
    // ========== 步骤 1: 构建 Skill 上下文 ==========
    const skillContext: SkillContext = {
      marketContext: context || '',
      conversationHistory: history || []
    }

    // ========== 步骤 2: Skill 路由匹配 ==========
    console.log(`[ChatApi] 开始 Skill 路由匹配: "${question.substring(0, 50)}..."`)
    const skillResult = await routeAndExecute(question, skillContext)
    
    let skillContent = ''
    let skillSources: string[] = []
    
    if (skillResult) {
      console.log(`[ChatApi] Skill 匹配成功: ${skillResult.metadata?.knowledgeUsed ? '知识库' : '其他 Skill'}`)
      skillContent = skillResult.content
      skillSources = skillResult.sources || []
    } else {
      console.log('[ChatApi] 没有匹配的 Skill')
    }

    // ========== 步骤 3: 对话历史处理 ==========
    let conversationSummary = ''
    
    if (history && history.length > 20) {
      // 对话历史过长，需要压缩
      console.log(`[ChatApi] 对话历史过长 (${history.length} 条)，需要压缩`)
      // 注意：摘要压缩需要在调用方进行，因为需要 LLM 回调
      // 这里暂时保留完整历史，由后续流程处理
    }
    
    // 获取最近的历史用于上下文（最多保留最近 10 轮对话 = 20 条消息）
    const recentHistory = history ? history.slice(-20) : []

    // ========== 步骤 4: 构建系统提示词 ==========
    const systemPrompt = buildSystemPrompt(skillContent, conversationSummary)

    // ========== 步骤 5: 构建完整消息数组 ==========
    const messages: Array<{ role: string; content: string }> = []
    
    // 系统提示词
    messages.push({ role: 'system', content: systemPrompt })
    
    // 对话历史
    for (const msg of recentHistory) {
      messages.push({ role: msg.role === 'assistant' ? 'assistant' : 'user', content: msg.content })
    }
    
    // 用户当前问题
    messages.push({ role: 'user', content: question })

    console.log(`[ChatApi] 发送 ${messages.length} 条消息给 LLM`)

    // ========== 步骤 6: 调用 LLM ==========
    const response = await sendAgentMessages(messages)

    if (onChunk) {
      onChunk(response)
    }
    
    if (onDone) {
      onDone(response)
    }

    return response
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
 * 向 AI 助手提问（支持对话摘要压缩）
 * 
 * 此方法接受一个 LLM 回调用于对话摘要，当对话历史过长时自动压缩
 *
 * @param question 用户问题
 * @param llmCallback 用于对话摘要的 LLM 回调
 * @param options 可选配置
 * @returns AI 回复
 */
export async function askAIWithSummary(
  question: string,
  llmCallback: (prompt: string) => Promise<string>,
  options: AskOptions = {}
): Promise<string> {
  const { context, history, onChunk, onDone, onError } = options

  // 检查 Agent 配置
  const agentConfig = getAgentConfig()
  if (!agentConfig) {
    return '请先在设置中配置 AI 服务。打开 设置 > Agent 大语言模型配置，填写服务 URL、协议和 API Key。'
  }

  try {
    // ========== 步骤 1: 构建 Skill 上下文 ==========
    const skillContext: SkillContext = {
      marketContext: context || '',
      conversationHistory: history || []
    }

    // ========== 步骤 2: Skill 路由匹配 ==========
    console.log(`[ChatApi] 开始 Skill 路由匹配: "${question.substring(0, 50)}..."`)
    const skillResult = await routeAndExecute(question, skillContext)
    
    let skillContent = ''
    
    if (skillResult) {
      console.log(`[ChatApi] Skill 匹配成功`)
      skillContent = skillResult.content
    }

    // ========== 步骤 3: 对话历史压缩 ==========
    let conversationSummary = ''
    let effectiveHistory = history || []
    
    if (effectiveHistory.length > 20) {
      // 需要压缩对话历史
      console.log(`[ChatApi] 对话历史过长 (${effectiveHistory.length} 条)，开始压缩`)
      
      const conversationText = effectiveHistory
        .slice(0, -10)  // 保留最近 10 条消息
        .map(m => `${m.role === 'user' ? '用户' : '助手'}: ${m.content}`)
        .join('\n\n')

      const summaryPrompt = `请将以下对话历史压缩为简洁的摘要，保留关键信息、上下文和重要决定：

${conversationText}

请用简洁的语言概括上述对话的核心内容和上下文。摘要：`

      try {
        conversationSummary = await llmCallback(summaryPrompt)
        effectiveHistory = effectiveHistory.slice(-10)  // 只保留最近 10 条
        console.log('[ChatApi] 对话历史已压缩')
      } catch (error) {
        console.error('[ChatApi] 对话摘要压缩失败，使用完整历史:', error)
        effectiveHistory = effectiveHistory.slice(-20)  // 回退到保留最近 20 条
      }
    }

    // ========== 步骤 4: 构建系统提示词 ==========
    const systemPrompt = buildSystemPrompt(skillContent, conversationSummary)

    // ========== 步骤 5: 构建完整消息数组 ==========
    const messages: Array<{ role: string; content: string }> = []
    
    // 系统提示词
    messages.push({ role: 'system', content: systemPrompt })
    
    // 对话历史（压缩后的）
    for (const msg of effectiveHistory) {
      messages.push({ role: msg.role === 'assistant' ? 'assistant' : 'user', content: msg.content })
    }
    
    // 用户当前问题
    messages.push({ role: 'user', content: question })

    console.log(`[ChatApi] 发送 ${messages.length} 条消息给 LLM`)

    // ========== 步骤 6: 调用 LLM ==========
    const response = await sendAgentMessages(messages)

    if (onChunk) {
      onChunk(response)
    }
    
    if (onDone) {
      onDone(response)
    }

    return response
  } catch (error) {
    console.error('[Chat] AI 请求失败:', error)

    if (onError) {
      onError(error as Error)
    }

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
