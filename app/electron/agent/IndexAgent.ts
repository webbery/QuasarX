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

const SUMMARY_PROMPT_TEMPLATE = `你是一位专业的知识摘要生成助手。请为以下内容生成一段 100-200 字的摘要。

要求：
1. 提取核心观点和关键结论
2. 保留重要的数据和事实
3. 语言简洁，逻辑清晰
4. 输出纯文本，不要使用 Markdown 格式
5. 直接输出摘要内容，不要添加任何前缀或解释

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
      maxTokens: 512,
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
   * 生成摘要
   * @param input 文档的完整文本内容
   * @param options LLM 配置 { llmConfig: LLMConfig }
   */
  async run(input: string, options?: Record<string, any>): Promise<string> {
    const llmConfig = options?.llmConfig as LLMConfig | undefined
    if (!llmConfig) {
      throw new Error('IndexAgent.run 需要 llmConfig 参数')
    }
    if (!llmConfig.url || !llmConfig.apiKey) {
      throw new Error('LLM 配置不完整：需要 url 和 apiKey')
    }

    const llm = this.createLLM(llmConfig)

    // 如果内容太长，截取前 4000 字（足够生成摘要）
    const contentToSummarize = input.length > 4000
      ? input.substring(0, 4000)
      : input

    const prompt = SUMMARY_PROMPT_TEMPLATE.replace('{content}', contentToSummarize)

    const response = await llm.invoke([
      { role: 'user' as const, content: prompt }
    ])

    const summary = typeof response.content === 'string'
      ? response.content.trim()
      : (response.content as any[]).map((c: any) => c.text ?? '').join('').trim()

    if (!summary) {
      throw new Error('LLM 返回空摘要')
    }

    return summary
  }
}
