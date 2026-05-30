/**
 * IndexAgent — 主进程索引/摘要生成 Agent
 *
 * 职责：
 * - 使用 LLM 为知识库文档生成 100-200 字摘要
 * - LLM 配置由 renderer 通过 options 传入（避免主进程访问 localStorage）
 *
 * 未来可扩展：文档分类、标签提取、知识图谱构建
 */

import { Agent } from './AgentRouter'
import { ChatOpenAI } from '@langchain/openai'
import { ChatAnthropic } from '@langchain/anthropic'

interface LLMConfig {
  url: string
  protocol: 'openai' | 'anthropic' | 'custom'
  apiKey: string
  model: string
}

const SUMMARY_PROMPT_TEMPLATE = `你是一位专业的知识摘要生成助手。请为以下内容生成一段 100-200 字的摘要，并提取 3-5 个关键标签。

## 输出格式（必须严格遵守）

你必须只输出以下格式的内容，不要包含任何其他文字、解释或思考过程：

摘要：[100-200字的摘要内容]
标签：[标签1], [标签2], [标签3]

## 要求

1. 提取核心观点和关键结论
2. 保留重要的数据和事实
3. 语言简洁，逻辑清晰
4. 直接输出摘要和标签，不要添加任何前缀、解释或思考过程
5. 标签应使用领域专业术语，用逗号分隔
6. **不要输出任何其他内容**，只输出"摘要："和"标签："两行

## 示例

摘要：本文介绍了一种基于深度学习的目标检测方法，通过使用改进的YOLOv5架构，在COCO数据集上实现了95.2%的mAP，比基线模型提升了3.5%。
标签：目标检测, 深度学习, YOLOv5, 计算机视觉

内容：
{content}`

export class IndexAgent implements Agent {
  readonly name = 'index'

  /**
   * 根据配置创建 LLM 实例
   */
  private createLLM(config: LLMConfig) {
    const commonOptions = {
      model: config.model,
      temperature: 0.3,  // 摘要生成用低温，更确定
      maxTokens: 1024,   // 增加 token 预算，确保有足够空间生成摘要
    }

    switch (config.protocol) {
      case 'openai':
        return new ChatOpenAI({
          ...commonOptions,
          configuration: { baseURL: config.url },
          apiKey: config.apiKey,
        })
      case 'anthropic':
        return new ChatAnthropic({
          ...commonOptions,
          apiKey: config.apiKey,
          anthropicApiUrl: config.url,
          // 禁用 thinking 模式，强制直接输出摘要
          thinking: { type: 'disabled' } as any,
        })
      default:
        // custom → OpenAI 兼容格式
        return new ChatOpenAI({
          ...commonOptions,
          configuration: { baseURL: config.url },
          apiKey: config.apiKey,
        })
    }
  }

  /**
   * 生成摘要和标签
   * @param input 文档的完整文本内容
   * @param options LLM 配置 { llmConfig: LLMConfig }
   * @returns { summary: string, tags: string[] }
   */
  async run(input: string, options?: Record<string, any>): Promise<{ summary: string; tags: string[] }> {
    const llmConfig = options?.llmConfig as LLMConfig | undefined
    if (!llmConfig) {
      throw new Error('IndexAgent.run 需要 llmConfig 参数')
    }
    if (!llmConfig.url || !llmConfig.apiKey) {
      throw new Error('LLM 配置不完整：需要 url 和 apiKey')
    }

    console.log(`[IndexAgent] LLM config: url=${llmConfig.url}, model=${llmConfig.model}, protocol=${llmConfig.protocol}`)

    const llm = this.createLLM(llmConfig)

    // 如果内容太长，截取前 4000 字（足够生成摘要）
    const contentToSummarize = input.length > 4000
      ? input.substring(0, 4000)
      : input

    console.log(`[IndexAgent] input length=${input.length}, using ${contentToSummarize.length} chars for summarization`)

    const prompt = SUMMARY_PROMPT_TEMPLATE.replace('{content}', contentToSummarize)

    console.log(`[IndexAgent] calling LLM with prompt length=${prompt.length}`)

    const response = await llm.invoke([
      { role: 'user' as const, content: prompt }
    ])

    // 调试日志：记录 LLM 原始响应
    console.log(`[IndexAgent] LLM response type: ${typeof response.content}`)
    if (typeof response.content === 'string') {
      console.log(`[IndexAgent] LLM response content (first 200 chars): ${response.content.substring(0, 200)}`)
    } else if (Array.isArray(response.content)) {
      console.log(`[IndexAgent] LLM response is array with ${response.content.length} items`)
      response.content.forEach((block: any, idx: number) => {
        console.log(`[IndexAgent]   block[${idx}]: type=${block.type}, text=${(block.text || '').substring(0, 100)}`)
      })
    } else {
      console.log(`[IndexAgent] LLM response content (raw):`, response.content)
    }

    const rawContent = typeof response.content === 'string'
      ? response.content.trim()
      : (() => {
          // 优先读取 text 块（实际回复内容）
          const textBlocks = response.content.filter((c: any) => c.type === 'text' || c.type === 'text_delta')
          if (textBlocks.length > 0) {
            return textBlocks.map((c: any) => c.text ?? '').join('').trim()
          }

          // 回退：检查 thinking 块是否包含所需格式（摘要：...标签：...）
          const thinkingBlocks = response.content.filter((c: any) => c.type === 'thinking')
          for (const block of thinkingBlocks) {
            const thinkingContent = block.thinking ?? block.text ?? ''
            // 检查是否包含目标格式
            if (thinkingContent.includes('摘要：') && thinkingContent.includes('标签：')) {
              console.log(`[IndexAgent] using thinking block (contains required format)`)
              return thinkingContent.trim()
            }
          }

          // 最终回退：读取所有块的内容
          console.log(`[IndexAgent] no text block found, reading all blocks as fallback`)
          return response.content.map((c: any) => {
            if (c.type === 'text' || c.type === 'text_delta') return c.text ?? ''
            if (c.type === 'thinking') return c.thinking ?? c.text ?? ''
            return ''
          }).join('').trim()
        })()

    // 额外调试：如果 rawContent 为空，打印所有块的详细信息
    if (rawContent.length === 0 && Array.isArray(response.content)) {
      console.error(`[IndexAgent] ❌ All blocks analysis:`)
      response.content.forEach((block: any, idx: number) => {
        console.error(`[IndexAgent]   block[${idx}]: type=${block.type}, hasText=${!!block.text}, textLength=${(block.text || '').length}`)
        if (block.text && block.text.length > 0) {
          console.error(`[IndexAgent]     text preview: ${(block.text as string).substring(0, 100)}`)
        }
      })
    }

    console.log(`[IndexAgent] parsed rawContent length=${rawContent.length}`)
    if (rawContent.length > 0) {
      console.log(`[IndexAgent] rawContent (first 300 chars): ${rawContent.substring(0, 300)}`)
    }

    if (!rawContent) {
      console.error(`[IndexAgent] ❌ LLM returned empty content!`)
      console.error(`[IndexAgent] Full response:`, JSON.stringify(response.content, null, 2))
      throw new Error('LLM 返回空内容')
    }

    // 解析摘要和标签
    let summary = ''
    let tags: string[] = []

    // ★ 修复：查找最后出现的"摘要："和"标签："（LLM 的最终输出）
    // 先找到最后一个"标签："的位置
    const lastTagsIndex = rawContent.lastIndexOf('标签：')
    const lastSummaryIndex = rawContent.lastIndexOf('摘要：')

    if (lastTagsIndex !== -1 && lastSummaryIndex !== -1 && lastSummaryIndex < lastTagsIndex) {
      // 摘要在标签之前，且都只出现一次或在最后出现
      // 提取摘要（从"摘要："到"标签："之间）
      summary = rawContent.substring(lastSummaryIndex + 3, lastTagsIndex).trim()
      // 提取标签（从"标签："到末尾）
      const tagsRaw = rawContent.substring(lastTagsIndex + 3).trim()
      tags = tagsRaw
        .split(/[,，、\n]/)  // 支持逗号、顿号、换行分隔
        .map(t => t.trim().replace(/^[\[\(【<{]*/, '').replace(/[\]\)】>}]*(\.\.\.)?$/, '').trim())
        .filter(t => t.length > 0 && t.length < 50)  // 过滤掉过长的文本（可能是错误匹配）
        .slice(0, 5)

      console.log(`[IndexAgent] parsed with lastIndexOf: summary length=${summary.length}, tags=[${tags.join(', ')}]`)
    } else {
      // 回退：使用原始正则
      const summaryMatch = rawContent.match(/摘要：([\s\S]*?)(?=标签：|$)/)
      const tagsMatch = rawContent.match(/标签：(.*)/)

      console.log(`[IndexAgent] parsing fallback: summaryMatch=${!!summaryMatch}, tagsMatch=${!!tagsMatch}`)

      if (summaryMatch) {
        summary = summaryMatch[1].trim()
      } else {
        summary = rawContent.substring(0, 200)
      }

      if (tagsMatch) {
        tags = tagsMatch[1]
          .split(/[,，、]/)
          .map(t => t.trim())
          .filter(t => t.length > 0 && t.length < 50)
          .slice(0, 5)
      }
    }

    console.log(`[IndexAgent] final result: summary="${summary.substring(0, 50)}...", tags=[${tags.join(', ')}]`)

    return { summary, tags }
  }
}
