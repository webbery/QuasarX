/**
 * AI 上下文构建器
 * 
 * 功能:
 * 1. 数据源注册表 - 记录各store的数据位置和元数据
 * 2. 智能模板选择 - 关键词匹配 + 数据源推断
 * 3. Prompt 生成 - 构建结构化的 AI 提示词
 * 4. Token 估算 - 预估提示词长度
 * 
 * 使用示例:
 * ```typescript
 * // 示例1: 自动匹配模板
 * const context = await aiContextBuilder.build({
 *   question: '帮我分析一下回测结果，夏普比率如何？'
 * })
 * 
 * // 示例2: 指定数据源和模板
 * const context = await aiContextBuilder.build({
 *   stores: ['data:quotes', 'data:shibor'],
 *   question: '基于当前行情和利率，建议如何调整持仓？',
 *   template: 'STRATEGY_ADVICE'
 * })
 * 
 * // 示例3: 按标签搜索
 * const context = await aiContextBuilder.build({
 *   tags: ['portfolio', 'risk'],
 *   question: '我的策略风险如何？'
 * })
 * ```
 */

// ==================== 类型定义 ====================

/** 数据源元数据 */
export interface DataSourceMeta {
  id: string                    // 唯一标识 (type:key)
  name: string                  // 可读名称
  description: string           // 数据描述
  tags: string[]                // 标签 (用于搜索)
  getter: () => any             // 数据获取函数
  searchMethod?: (query: string) => Promise<any>  // 可选的搜索方法
  updateFrequency?: string      // 更新频率
  aiContext?: string            // AI 上下文说明
}

/** Prompt 模板 */
export interface PromptTemplate {
  type: string
  systemPrompt: string
  userPromptTemplate: string
  dataPlaceholders: string[]
}

/** 构建请求 */
export interface BuildRequest {
  question: string              // 用户问题
  stores?: string[]             // 指定数据源 ID 列表
  tags?: string[]               // 按标签搜索
  template?: string             // 指定模板 (可选)
  options?: {
    includeRawData?: boolean    // 是否包含原始数据
    maxDataLength?: number      // 最大数据长度
    minScore?: number           // 最低置信度阈值
  }
}

/** 构建结果 */
export interface BuildResult {
  prompt: string                // 生成的 Prompt
  template: string              // 使用的模板
  dataSources: string[]         // 使用的数据源
  contextSummary: string        // 上下文摘要
  tokenEstimate: number         // 预估 token 数
}

// ==================== Prompt 模板定义 ====================

/** 预定义模板 */
export const PROMPT_TEMPLATES: Record<string, PromptTemplate> = {
  DATA_ANALYSIS: {
    type: 'DATA_ANALYSIS',
    systemPrompt: '你是一个专业的量化交易数据分析师。请基于提供的数据，给出详细分析和洞察。',
    userPromptTemplate: `## 数据上下文

{context_summary}

## 详细数据

{data_details}

## 用户问题

{user_question}

请从以下角度分析：
1. 数据趋势和模式识别
2. 异常值和关键指标
3. 数据质量和可靠性评估
4. 建议的后续行动`,
    dataPlaceholders: ['context_summary', 'data_details', 'user_question']
  },

  STRATEGY_ADVICE: {
    type: 'STRATEGY_ADVICE',
    systemPrompt: '你是一个资深量化策略顾问。基于当前市场数据和持仓情况，提供策略优化建议。',
    userPromptTemplate: `## 当前状态

### 持仓情况
{portfolio_data}

### 市场数据
{market_data}

### 相关知识
{knowledge_data}

## 用户问题

{user_question}

请提供：
1. 当前策略评估（优势/劣势）
2. 风险点识别和预警
3. 具体优化建议（参数/仓位/时机）
4. 预期效果和改进空间`,
    dataPlaceholders: ['portfolio_data', 'market_data', 'knowledge_data', 'user_question']
  },

  BACKTEST_ANALYSIS: {
    type: 'BACKTEST_ANALYSIS',
    systemPrompt: '你是一个专业的量化策略回测分析师。请基于回测数据，深入分析策略表现，识别优势和风险，并给出改进建议。',
    userPromptTemplate: `## 回测概况

### 策略信息
{strategy_info}

### 回测参数
{backtest_params}

## 业绩指标

### 收益指标
{return_metrics}

### 风险指标
{risk_metrics}

### 交易统计
{trade_stats}

## 用户问题

{user_question}

请从以下角度分析：
1. **策略总体评估**：夏普比率、最大回撤、年化收益
2. **收益来源分析**：哪些交易贡献了主要收益
3. **风险识别**：最大亏损场景、回撤持续时间
4. **改进建议**：参数优化、止损机制、仓位管理
5. **适用场景**：该策略适合的市场环境`,
    dataPlaceholders: ['strategy_info', 'backtest_params', 'return_metrics', 'risk_metrics', 'trade_stats', 'user_question']
  },

  MARKET_PREDICTION: {
    type: 'MARKET_PREDICTION',
    systemPrompt: '你是一个资深量化市场分析师。请基于历史数据、技术指标和宏观因素，对市场走势进行预测分析。',
    userPromptTemplate: `## 市场现状

### 行情数据
{market_data}

### 技术指标
{technical_indicators}

### 资金流向
{fund_flow}

## 宏观环境

### 利率数据
{interest_rates}

### 板块轮动
{sector_rotation}

## 用户问题

{user_question}

请提供：
1. **趋势判断**：短期/中期/长期趋势
2. **关键位识别**：支撑位、阻力位、突破点
3. **概率评估**：上涨/下跌/横盘的概率
4. **风险提示**：潜在风险因素
5. **操作建议**：具体交易策略`,
    dataPlaceholders: ['market_data', 'technical_indicators', 'fund_flow', 'interest_rates', 'sector_rotation', 'user_question']
  }
}

// ==================== 关键词匹配引擎 ====================

/** 模板关键词映射 */
const TEMPLATE_KEYWORDS: Record<string, string[]> = {
  BACKTEST_ANALYSIS: [
    '回测', 'backtest', 'back-test', '回测结果', '回测报告',
    '策略表现', '夏普', '夏普比率', '最大回撤', '年化收益', '胜率', '盈亏比',
    '净值曲线', '回撤分析', '交易统计', '收益风险比', 'calmar', 'sortino'
  ],
  
  MARKET_PREDICTION: [
    '预测', 'prediction', '走势', '趋势', '未来', '明天', '下周', '下月',
    '大盘', '指数', '突破', '支撑', '阻力', '上涨', '下跌',
    '看涨', '看跌', '震荡', '行情判断', '点位', '目标位'
  ],
  
  STRATEGY_ADVICE: [
    '策略', 'strategy', '建议', '优化', '调整', '改进', '改善',
    '持仓', '仓位', '配置', '选股', '择时', '止损',
    '止盈', '风控', '风险', '敞口', '对冲', '调仓',
    '加仓', '减仓', '平仓', '建仓'
  ],
  
  DATA_ANALYSIS: [
    '分析', 'analysis', '数据', '统计', '对比', '比较',
    '多少', '什么', '如何', '为什么', '怎么样'
  ]
}

/** 数据源到模板的映射 */
const DATA_SOURCE_TEMPLATE_MAP: Record<string, string> = {
  'data:backtest_results': 'BACKTEST_ANALYSIS',
  'data:backtest_metrics': 'BACKTEST_ANALYSIS',
  'data:backtest': 'BACKTEST_ANALYSIS',
  
  'data:quotes': 'MARKET_PREDICTION',
  'data:market_index': 'MARKET_PREDICTION',
  'data:kline': 'MARKET_PREDICTION',
  'crawler:shibor': 'MARKET_PREDICTION',
  'crawler:sector_flow': 'MARKET_PREDICTION',
  
  'data:portfolio': 'STRATEGY_ADVICE',
  'data:strategy_state': 'STRATEGY_ADVICE',
  'knowledge:risk_models': 'STRATEGY_ADVICE',
  'knowledge:strategy_guide': 'STRATEGY_ADVICE'
}

/**
 * 通过关键词匹配模板
 */
function matchTemplateByKeywords(question: string): { template: string; score: number } {
  const lowerQuestion = question.toLowerCase()
  let bestMatch = { template: 'DATA_ANALYSIS', score: 0 }
  
  for (const [template, keywords] of Object.entries(TEMPLATE_KEYWORDS)) {
    const matchCount = keywords.filter(kw => 
      lowerQuestion.includes(kw.toLowerCase())
    ).length
    
    if (matchCount > 0) {
      const score = matchCount / Math.sqrt(keywords.length)  // 归一化
      if (score > bestMatch.score) {
        bestMatch = { template, score }
      }
    }
  }
  
  return bestMatch
}

/**
 * 从数据源推断模板
 */
function inferTemplateFromSources(stores: string[]): string {
  for (const store of stores) {
    if (DATA_SOURCE_TEMPLATE_MAP[store]) {
      return DATA_SOURCE_TEMPLATE_MAP[store]
    }
  }
  return 'DATA_ANALYSIS'
}

// ==================== 数据源注册表 ====================

export class DataSourceRegistry {
  private sources: Map<string, DataSourceMeta> = new Map()

  /** 注册数据源 */
  register(meta: DataSourceMeta) {
    this.sources.set(meta.id, meta)
  }

  /** 获取数据源 */
  get(id: string): DataSourceMeta | undefined {
    return this.sources.get(id)
  }

  /** 按标签搜索 */
  searchByTags(tags: string[]): DataSourceMeta[] {
    const results: DataSourceMeta[] = []
    
    for (const source of this.sources.values()) {
      const hasMatch = tags.some(tag => 
        source.tags.some(t => t.toLowerCase().includes(tag.toLowerCase()))
      )
      if (hasMatch) {
        results.push(source)
      }
    }
    
    return results
  }

  /** 按关键词搜索 */
  searchByKeyword(query: string): DataSourceMeta[] {
    const lowerQuery = query.toLowerCase()
    const results: DataSourceMeta[] = []
    
    for (const source of this.sources.values()) {
      const matchName = source.name.toLowerCase().includes(lowerQuery)
      const matchDesc = source.description.toLowerCase().includes(lowerQuery)
      const matchTags = source.tags.some(t => t.toLowerCase().includes(lowerQuery))
      
      if (matchName || matchDesc || matchTags) {
        results.push(source)
      }
    }
    
    return results
  }

  /** 获取所有数据源 */
  getAllSources(): DataSourceMeta[] {
    return Array.from(this.sources.values())
  }

  /** 获取数据源 ID 列表 */
  getSourceIds(): string[] {
    return Array.from(this.sources.keys())
  }
}

// ==================== AIContextBuilder 核心类 ====================

export class AIContextBuilder {
  public registry: DataSourceRegistry = new DataSourceRegistry()

  /**
   * 构建 AI 上下文
   */
  async build(request: BuildRequest): Promise<BuildResult> {
    // Step 1: 智能选择模板
    const template = this.selectTemplate(request)
    
    // Step 2: 收集数据源
    const sources = await this.collectDataSources(request)
    
    // Step 3: 获取实际数据
    const dataMap = this.fetchData(sources)
    
    // Step 4: 构建上下文摘要
    const summary = this.buildSummary(sources, dataMap)
    
    // Step 5: 填充模板
    const prompt = this.fillTemplate(template, {
      dataMap,
      summary,
      question: request.question
    })
    
    // Step 6: 估算 token
    const tokenEstimate = this.estimateTokens(prompt)
    
    return {
      prompt,
      template: template.type,
      dataSources: sources.map(s => s.id),
      contextSummary: summary,
      tokenEstimate
    }
  }

  /**
   * 智能选择模板
   */
  private selectTemplate(request: BuildRequest): PromptTemplate {
    // 优先级1: 用户显式指定
    if (request.template) {
      const tpl = PROMPT_TEMPLATES[request.template]
      if (tpl) {
        console.log(`[AIContextBuilder] 使用指定模板: ${request.template}`)
        return tpl
      }
    }
    
    // 优先级2: 关键词匹配
    const keywordMatch = matchTemplateByKeywords(request.question)
    const minScore = request.options?.minScore || 0.2
    
    if (keywordMatch.score >= minScore) {
      const tpl = PROMPT_TEMPLATES[keywordMatch.template]
      if (tpl) {
        console.log(`[AIContextBuilder] 关键词匹配模板: ${keywordMatch.template} (score: ${keywordMatch.score.toFixed(2)})`)
        return tpl
      }
    }
    
    // 优先级3: 数据源推断
    if (request.stores && request.stores.length > 0) {
      const templateName = inferTemplateFromSources(request.stores)
      const tpl = PROMPT_TEMPLATES[templateName]
      if (tpl) {
        console.log(`[AIContextBuilder] 数据源推断模板: ${templateName}`)
        return tpl
      }
    }
    
    // 默认模板
    console.log(`[AIContextBuilder] 使用默认模板: DATA_ANALYSIS`)
    return PROMPT_TEMPLATES.DATA_ANALYSIS
  }

  /**
   * 收集数据源
   */
  private async collectDataSources(request: BuildRequest): Promise<DataSourceMeta[]> {
    const sources: DataSourceMeta[] = []
    const seen = new Set<string>()

    // 方式1: 直接指定
    if (request.stores) {
      for (const storeId of request.stores) {
        const source = this.registry.get(storeId)
        if (source && !seen.has(source.id)) {
          sources.push(source)
          seen.add(source.id)
        }
      }
    }

    // 方式2: 按标签搜索
    if (request.tags) {
      const matched = this.registry.searchByTags(request.tags)
      for (const source of matched) {
        if (!seen.has(source.id)) {
          sources.push(source)
          seen.add(source.id)
        }
      }
    }

    // 如果没有指定任何数据源，返回所有已注册的
    if (sources.length === 0) {
      return this.registry.getAllSources()
    }

    return sources
  }

  /**
   * 获取数据
   */
  private fetchData(sources: DataSourceMeta[]): Record<string, any> {
    const dataMap: Record<string, any> = {}

    for (const source of sources) {
      try {
        dataMap[source.id] = source.getter()
      } catch (error) {
        console.error(`[AIContextBuilder] 获取数据失败 ${source.id}:`, error)
        dataMap[source.id] = null
      }
    }

    return dataMap
  }

  /**
   * 构建上下文摘要
   */
  private buildSummary(sources: DataSourceMeta[], dataMap: Record<string, any>): string {
    const lines: string[] = []

    for (const source of sources) {
      const data = dataMap[source.id]
      lines.push(`### ${source.name}`)
      lines.push(`- 标识: ${source.id}`)
      lines.push(`- 描述: ${source.description}`)
      
      if (source.aiContext) {
        lines.push(`- 说明: ${source.aiContext}`)
      }
      
      if (source.updateFrequency) {
        lines.push(`- 更新频率: ${source.updateFrequency}`)
      }

      // 数据特征
      if (data) {
        if (Array.isArray(data)) {
          lines.push(`- 记录数: ${data.length}`)
          if (data.length > 0 && typeof data[0] === 'object') {
            lines.push(`- 字段: ${Object.keys(data[0]).join(', ')}`)
          }
        } else if (typeof data === 'object') {
          lines.push(`- 字段: ${Object.keys(data).join(', ')}`)
        }
      }

      lines.push('')
    }

    return lines.join('\n')
  }

  /**
   * 填充模板
   */
  private fillTemplate(
    template: PromptTemplate,
    context: {
      dataMap: Record<string, any>
      summary: string
      question: string
    }
  ): string {
    let prompt = `# 系统提示\n${template.systemPrompt}\n\n`
    prompt += `# 数据上下文\n\n${context.summary}\n`

    // 根据模板类型填充数据
    const dataByType = this.categorizeData(context.dataMap)

    // 填充数据占位符
    let userPrompt = template.userPromptTemplate

    // 通用替换
    userPrompt = userPrompt.replace('{context_summary}', context.summary)
    userPrompt = userPrompt.replace('{user_question}', context.question)

    // 分类数据替换
    for (const [key, value] of Object.entries(dataByType)) {
      const placeholder = `{${key}}`
      if (userPrompt.includes(placeholder)) {
        userPrompt = userPrompt.replace(placeholder, this.formatData(value))
      }
    }

    prompt += userPrompt

    return prompt
  }

  /**
   * 按类型分类数据
   */
  private categorizeData(dataMap: Record<string, any>): Record<string, any> {
    const categorized: Record<string, any> = {
      portfolio_data: null,
      market_data: null,
      knowledge_data: null,
      strategy_info: null,
      backtest_params: null,
      return_metrics: null,
      risk_metrics: null,
      trade_stats: null,
      technical_indicators: null,
      fund_flow: null,
      interest_rates: null,
      sector_rotation: null
    }

    for (const [id, data] of Object.entries(dataMap)) {
      // 根据 ID 推断类型
      if (id.includes('portfolio') || id.includes('position')) {
        categorized.portfolio_data = data
      } else if (id.includes('quote') || id.includes('market') || id.includes('kline')) {
        categorized.market_data = data
      } else if (id.includes('knowledge') || id.includes('document')) {
        categorized.knowledge_data = data
      } else if (id.includes('strategy')) {
        categorized.strategy_info = data
      } else if (id.includes('backtest')) {
        categorized.backtest_params = data
      } else if (id.includes('return') || id.includes('profit')) {
        categorized.return_metrics = data
      } else if (id.includes('risk') || id.includes('drawdown')) {
        categorized.risk_metrics = data
      } else if (id.includes('trade') || id.includes('transaction')) {
        categorized.trade_stats = data
      } else if (id.includes('indicator') || id.includes('technical')) {
        categorized.technical_indicators = data
      } else if (id.includes('fund_flow') || id.includes('sector')) {
        categorized.fund_flow = data
      } else if (id.includes('shibor') || id.includes('interest') || id.includes('rate')) {
        categorized.interest_rates = data
      }
    }

    return categorized
  }

  /**
   * 格式化数据为字符串
   */
  private formatData(data: any): string {
    if (!data) return '无数据'
    
    if (typeof data === 'string') return data
    if (typeof data === 'number') return data.toString()
    
    try {
      return JSON.stringify(data, null, 2)
    } catch {
      return String(data)
    }
  }

  /**
   * 估算 token 数量
   */
  estimateTokens(text: string): number {
    // 中文约 1.5-2 个 token/字，英文约 0.25 个 token/字
    const chineseChars = (text.match(/[\u4e00-\u9fa5]/g) || []).length
    const otherChars = text.length - chineseChars
    return Math.ceil(chineseChars * 1.8 + otherChars * 0.25)
  }
}

// ==================== 导出单例 ====================

export const aiContextBuilder = new AIContextBuilder()
