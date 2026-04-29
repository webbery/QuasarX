/**
 * 默认知识库 Skill
 * 根据用户问题检索知识库内容，并根据匹配程度决定是否使用知识库
 * 
 * 此 Skill 作为兜底 Skill，优先级最低，在其他 Skill 都不匹配时执行
 */

import type { Skill, SkillContext, SkillResult } from '@/types/skill'
import { ragRetrieve } from '@/ts/rag'

// 知识库匹配阈值 - 低于此值认为知识库没有相关内容
const KNOWLEDGE_SIMILARITY_THRESHOLD = 0.4

export const defaultKnowledgeSkill: Skill = {
  id: 'default-knowledge',
  name: '知识库检索',
  description: '从用户本地知识库中检索与问题相关的内容，用于回答用户的问题',
  keywords: [],  // 空关键词列表，依赖 alwaysInvoke 始终匹配
  enabled: true,
  
  /**
   * 始终返回 true，作为兜底 Skill 始终执行
   * 但会根据知识库匹配结果决定是否使用知识库内容
   */
  shouldInvoke: (_input: string, _context: SkillContext): boolean => {
    return true
  },
  
  /**
   * 执行知识库检索
   * 根据相似度判断是否使用知识库：
   * - 有相关内容 → 返回知识库内容 + 来源
   * - 无相关内容 → 返回"未找到相关内容"，让 LLM 使用自身知识回答
   */
  execute: async (input: string, _context: SkillContext): Promise<SkillResult> => {
    try {
      console.log(`[KnowledgeSkill] 检索知识库: "${input.substring(0, 50)}..."`)
      
      // 执行向量检索
      const { sources, context } = await ragRetrieve(input, 5)
      
      // 检查是否有高匹配度的结果
      const hasRelevantContent = sources.length > 0 && 
        sources.some(s => s.similarity >= KNOWLEDGE_SIMILARITY_THRESHOLD)
      
      if (hasRelevantContent) {
        // 过滤低相似度结果
        const relevantSources = sources.filter(s => 
          s.similarity >= KNOWLEDGE_SIMILARITY_THRESHOLD
        )
        
        // 构建知识库内容
        const knowledgeContent = relevantSources.map((source, idx) => {
          return `--- 文档 ${idx + 1}: ${source.fileName} (相似度: ${(source.similarity * 100).toFixed(1)}%) ---\n${source.content}`
        }).join('\n\n')
        
        console.log(`[KnowledgeSkill] 找到 ${relevantSources.length} 条相关知识`)
        
        return {
          content: `【知识库相关内容】\n\n${knowledgeContent}`,
          sources: relevantSources.map(s => `出自: ${s.fileName}`),
          shouldContinueChat: true,
          metadata: {
            knowledgeUsed: true,
            sourceCount: relevantSources.length,
            avgSimilarity: relevantSources.reduce((sum, s) => sum + s.similarity, 0) / relevantSources.length
          }
        }
      }
      
      // 没有相关知识
      console.log('[KnowledgeSkill] 未找到相关知识')
      
      return {
        content: '【知识库未找到相关内容】\n\n当前知识库中没有与您的问题直接相关的内容。以下将基于通用知识进行回答。',
        sources: [],
        shouldContinueChat: true,
        metadata: {
          knowledgeUsed: false,
          sourceCount: 0
        }
      }
      
    } catch (error) {
      console.error('[KnowledgeSkill] 检索失败:', error)
      
      // 检索失败时也返回兜底结果
      return {
        content: '【知识库检索失败】\n\n知识库当前不可用，将基于通用知识回答您的问题。',
        sources: [],
        shouldContinueChat: true,
        metadata: {
          knowledgeUsed: false,
          error: true
        }
      }
    }
  },
  
  /**
   * 最低优先级，让其他具体 Skill 优先匹配
   */
  priority: () => 0.1
}
