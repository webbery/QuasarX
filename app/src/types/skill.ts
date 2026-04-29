/**
 * Skill 系统类型定义
 * 用于模块化扩展 Agent 的特定能力
 */

import type { ChatMessage } from '@/stores/chatStore'

/**
 * Skill 上下文 - 执行 Skill 时提供的背景信息
 */
export interface SkillContext {
  /** 市场背景（行情数据、板块信息等） */
  marketContext: string
  /** 账户数据 */
  accountData?: Record<string, any>
  /** 知识库内容（检索到的） */
  knowledgeBase?: string
  /** 对话历史 */
  conversationHistory: ChatMessage[]
}

/**
 * Skill 执行结果
 */
export interface SkillResult {
  /** Skill 返回的内容 */
  content: string
  /** 引用来源（可选） */
  sources?: string[]
  /** 是否继续对话（多步 Skill 场景） */
  shouldContinueChat: boolean
  /** 额外元数据（可选） */
  metadata?: Record<string, any>
}

/**
 * Skill 接口 - 每个 Skill 必须实现的契约
 */
export interface Skill {
  /** Skill 唯一标识 */
  id: string
  /** Skill 名称（用于展示和调试） */
  name: string
  /** Skill 描述（用于路由判断，应包含关键词和功能描述） */
  description: string
  /** 触发关键词列表 */
  keywords: string[]
  /** 是否启用 */
  enabled: boolean
  
  /**
   * 判断是否应该调用此 Skill
   * @param input 用户输入
   * @param context Skill 上下文
   * @returns 是否应该调用
   */
  shouldInvoke: (input: string, context: SkillContext) => boolean
  
  /**
   * 执行 Skill 逻辑
   * @param input 用户输入
   * @param context Skill 上下文
   * @returns Skill 执行结果
   */
  execute: (input: string, context: SkillContext) => Promise<SkillResult>
  
  /**
   * Skill 优先级（0-1），数值越高优先级越高
   * 用于多个 Skill 都匹配时决定调用顺序
   * 默认返回 0.5，具体 Skill 可以覆盖
   */
  priority?: () => number
}
