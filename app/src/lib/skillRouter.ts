/**
 * Skill 路由管理器
 * 根据用户输入匹配最合适的 Skill 并执行
 */

import type { Skill, SkillContext, SkillResult } from '@/types/skill'

// Skill 注册表
const skillsRegistry = new Map<string, Skill>()

// 匹配阈值 - 低于此分数则认为没有匹配的 Skill
const MATCH_THRESHOLD = 0.3

/**
 * 注册一个 Skill
 * @param skill Skill 实例
 */
export function registerSkill(skill: Skill): void {
  if (skillsRegistry.has(skill.id)) {
    console.warn(`[SkillRouter] Skill "${skill.id}" 已存在，将被覆盖`)
  }
  skillsRegistry.set(skill.id, skill)
  console.log(`[SkillRouter] 已注册 Skill: ${skill.name} (${skill.id})`)
}

/**
 * 取消注册 Skill
 * @param skillId Skill ID
 */
export function unregisterSkill(skillId: string): void {
  skillsRegistry.delete(skillId)
  console.log(`[SkillRouter] 已注销 Skill: ${skillId}`)
}

/**
 * 获取所有已注册的 Skill
 */
export function getRegisteredSkills(): Skill[] {
  return Array.from(skillsRegistry.values())
}

/**
 * 路由判断并执行匹配的 Skill
 * @param input 用户输入
 * @param context Skill 上下文
 * @returns Skill 执行结果，如果没有匹配则返回 null
 */
export async function routeAndExecute(
  input: string,
  context: SkillContext
): Promise<SkillResult | null> {
  const enabledSkills = Array.from(skillsRegistry.values()).filter(s => s.enabled)
  
  if (enabledSkills.length === 0) {
    console.log('[SkillRouter] 没有已启用的 Skill')
    return null
  }
  
  // 计算每个 Skill 的匹配分数
  const scored = await Promise.all(
    enabledSkills.map(async (skill) => {
      const shouldInvoke = skill.shouldInvoke(input, context)
      const score = calculateSkillScore(skill, input)
      
      return {
        skill,
        shouldInvoke,
        score,
        // 综合分数：如果 shouldInvoke 返回 false，分数直接为 0
        finalScore: shouldInvoke ? score : 0
      }
    })
  )
  
  // 按分数降序排序
  scored.sort((a, b) => b.finalScore - a.finalScore)
  
  // 获取最高分
  const best = scored[0]
  
  if (best && best.finalScore >= MATCH_THRESHOLD) {
    console.log(`[SkillRouter] 匹配到 Skill: ${best.skill.name} (分数: ${best.finalScore.toFixed(2)})`)
    
    try {
      const result = await best.skill.execute(input, context)
      console.log(`[SkillRouter] Skill "${best.skill.name}" 执行完成`)
      return result
    } catch (error) {
      console.error(`[SkillRouter] Skill "${best.skill.name}" 执行失败:`, error)
      return null
    }
  }
  
  console.log(`[SkillRouter] 没有匹配的 Skill（最高分: ${best?.finalScore.toFixed(2) ?? 0}）`)
  return null
}

/**
 * 计算 Skill 匹配分数
 * 使用关键词匹配 + 描述相似度混合评分
 * 
 * @param skill Skill 实例
 * @param input 用户输入
 * @returns 0-1 之间的分数
 */
function calculateSkillScore(skill: Skill, input: string): number {
  const inputLower = input.toLowerCase().trim()
  
  if (!inputLower) {
    return 0
  }
  
  // 1. 关键词匹配分数（权重 60%）
  const keywordScore = calculateKeywordMatch(skill.keywords, inputLower)
  
  // 2. 描述相似度分数（权重 40%）
  const descriptionScore = calculateTextSimilarity(inputLower, skill.description)
  
  // 3. 优先级加成（如果有定义）
  const priorityBonus = skill.priority ? (skill.priority() - 0.5) * 0.1 : 0
  
  // 综合分数
  const finalScore = keywordScore * 0.6 + descriptionScore * 0.4 + priorityBonus
  
  // 限制在 0-1 之间
  return Math.max(0, Math.min(1, finalScore))
}

/**
 * 计算关键词匹配分数
 * 使用覆盖度计算：匹配的关键词数量 / 总关键词数量
 */
function calculateKeywordMatch(keywords: string[], input: string): number {
  if (keywords.length === 0) {
    return 0
  }
  
  let matchedCount = 0
  let maxMatchLength = 0
  
  for (const keyword of keywords) {
    const keywordLower = keyword.toLowerCase()
    if (input.includes(keywordLower)) {
      matchedCount++
      // 记录最长匹配的关键词长度（用于加权）
      maxMatchLength = Math.max(maxMatchLength, keywordLower.length)
    }
  }
  
  // 基础匹配率
  const matchRate = matchedCount / keywords.length
  
  // 长关键词加权（匹配更长关键词给予额外加分）
  const lengthBonus = maxMatchLength > 4 ? 0.1 : 0
  
  return Math.min(1, matchRate + lengthBonus)
}

/**
 * 计算文本相似度（简化版 Jaccard 相似度）
 */
function calculateTextSimilarity(text1: string, text2: string): number {
  const words1 = tokenize(text1)
  const words2 = tokenize(text2)
  
  if (words1.size === 0 || words2.size === 0) {
    return 0
  }
  
  // 计算交集
  const intersection = new Set([...words1].filter(w => words2.has(w)))
  
  // 计算并集
  const union = new Set([...words1, ...words2])
  
  return intersection.size / union.size
}

/**
 * 简单的中文分词（按字符和常用词边界分割）
 * 对于中文，我们按字符和常见双字词进行处理
 */
function tokenize(text: string): Set<string> {
  const tokens = new Set<string>()
  
  // 移除标点符号
  const cleaned = text.replace(/[^\w\s\u4e00-\u9fff]/g, ' ')
  
  // 添加单个字符（用于中文）
  for (const char of cleaned) {
    if (char.trim()) {
      tokens.add(char)
    }
  }
  
  // 添加英文单词
  const words = cleaned.match(/[a-zA-Z]+/g)
  if (words) {
    words.forEach(w => tokens.add(w.toLowerCase()))
  }
  
  // 添加双字组合（中文）
  const chineseChars = cleaned.replace(/[^\u4e00-\u9fff]/g, '')
  for (let i = 0; i < chineseChars.length - 1; i++) {
    tokens.add(chineseChars.substring(i, i + 2))
  }
  
  return tokens
}
