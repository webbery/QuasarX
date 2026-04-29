/**
 * RAG (Retrieval-Augmented Generation) 检索增强生成
 * 基于知识库中的 PDF 文本块，为用户提供带有背景知识的问答能力
 */

import { search as vectorSearch, type SearchResult } from '../lib/vectorDB';
import { useKnowledgeStore } from '../stores/knowledgeStore';

export interface RAGContext {
  query: string;          // 用户问题
  topK?: number;          // 检索文档块数量
  systemPrompt?: string;  // 系统提示词
}

export interface RAGResponse {
  answer: string;         // LLM 回答
  sources: RAGSource[];   // 引用来源
  context: string;        // 发送给 LLM 的上下文
}

export interface RAGSource {
  fileName: string;       // 来源文件名
  chunkIndex: number;     // 块索引
  content: string;        // 文本内容
  similarity: number;     // 相似度
}

/**
 * 构建系统提示词
 * 根据是否有知识库内容，动态调整提示词
 */
export function buildSystemPrompt(
  skillResultContent?: string,
  conversationSummary?: string
): string {
  const hasKnowledge = skillResultContent && 
                       skillResultContent.includes('【知识库相关内容】');
  
  return `你拥有丰富的金融专业知识和市场分析能力。

=== 你的知识来源 ===
1. **自身知识**：你拥有广泛的金融、投资、量化交易等领域的通用知识
2. **知识库**（如下方提供）：来自用户上传的本地文档
3. **对话历史**（如下方提供）：之前的对话内容

${hasKnowledge ? `
=== 回答规则 ===
- 下方提供了"知识库相关内容"，请优先参考它来回答
- 回答时请标明信息来源出自哪个文档
- 如果知识库内容不够充分，可以补充你自己的专业知识
- 回答时应明确指出哪些信息来自知识库，哪些来自你自己的知识
` : `
=== 回答规则 ===
- 当前知识库中没有找到与用户问题直接相关的内容
- 请使用你自己的专业知识和通用知识回答用户问题
- 如果涉及专业领域，请明确说明这是基于你的专业知识而非知识库
`}

${conversationSummary ? `
=== 对话历史摘要 ===
${conversationSummary}

请结合之前的对话内容，保持上下文连贯性。
` : ''}

请回答用户的问题。`;
}

/**
 * 组装 RAG Prompt（旧版兼容接口）
 */
function buildPrompt(
  query: string, 
  sources: RAGSource[], 
  systemPrompt: string
): { prompt: string; context: string } {
  // 构建上下文文本
  let contextText = '';
  sources.forEach((source, idx) => {
    contextText += `\n--- [文档 ${idx + 1}] ${source.fileName} (片段 ${source.chunkIndex + 1}, 相似度: ${(source.similarity * 100).toFixed(1)}%) ---\n`;
    contextText += source.content;
    contextText += '\n';
  });

  const prompt = `${systemPrompt}

=== 知识库上下文 ===
${contextText}

=== 用户问题 ===
${query}

请回答用户的问题。`;

  return {
    prompt,
    context: contextText,
  };
}

/**
 * 执行 RAG 检索（仅检索，不调用 LLM）
 * @param query 用户查询
 * @param topK 返回结果数量
 * @returns RAG 检索结果（不含 LLM 回答）
 */
export async function ragRetrieve(
  query: string,
  topK: number = 5
): Promise<{ sources: RAGSource[]; context: string }> {
  // 执行向量检索
  const results: SearchResult[] = await vectorSearch(query, topK);

  // 更新命中次数
  const knowledgeStore = useKnowledgeStore();
  const hitDocIds = new Set<string>();
  for (const result of results) {
    if (!hitDocIds.has(result.chunk.docId)) {
      hitDocIds.add(result.chunk.docId);
      knowledgeStore.incrementHitCount(result.chunk.docId);
    }
  }

  // 转换为来源格式
  const sources: RAGSource[] = results.map(r => ({
    fileName: r.chunk.fileName,
    chunkIndex: r.chunk.chunkIndex,
    content: r.chunk.content,
    similarity: r.similarity,
  }));

  // 构建上下文字符串
  let contextText = '';
  sources.forEach((source, idx) => {
    contextText += `\n--- [文档 ${idx + 1}] ${source.fileName} (片段 ${source.chunkIndex + 1}) ---\n`;
    contextText += source.content;
    contextText += '\n';
  });

  return { sources, context: contextText };
}

/**
 * 执行完整的 RAG 流程（检索 + LLM 生成）
 * @param query 用户查询
 * @param llmCallback LLM 调用回调函数（由调用方提供具体的 LLM API 调用逻辑）
 * @param topK 检索文档块数量
 * @param systemPrompt 自定义系统提示词（可选，如果不提供则使用 buildSystemPrompt）
 * @param skillResult Skill 执行结果（可选）
 * @param conversationSummary 对话历史摘要（可选）
 * @returns RAG 完整响应
 */
export async function ragQuery(
  query: string,
  llmCallback: (prompt: string) => Promise<string>,
  topK: number = 5,
  systemPrompt?: string,
  skillResult?: string,
  conversationSummary?: string
): Promise<RAGResponse> {
  // 1. 检索阶段
  const { sources, context } = await ragRetrieve(query, topK);

  // 2. 组装 Prompt
  let finalPrompt: string;
  
  if (systemPrompt) {
    // 使用自定义系统提示词（旧版兼容）
    const { prompt } = buildPrompt(query, sources, systemPrompt);
    finalPrompt = prompt;
  } else {
    // 使用新版提示词结构
    const skillContent = skillResult || (sources.length > 0 ? buildKnowledgeContext(sources) : '');
    finalPrompt = buildSystemPrompt(skillContent, conversationSummary);
    
    // 添加用户问题
    finalPrompt += `\n\n=== 用户问题 ===\n${query}\n\n请回答用户的问题。`;
  }

  // 3. 调用 LLM
  const answer = await llmCallback(finalPrompt);

  return {
    answer,
    sources,
    context,
  };
}

/**
 * 构建知识库上下文字符串
 */
function buildKnowledgeContext(sources: RAGSource[]): string {
  if (sources.length === 0) return '';
  
  const knowledgeContent = sources.map((source, idx) => {
    return `--- 文档 ${idx + 1}: ${source.fileName} (相似度: ${(source.similarity * 100).toFixed(1)}%) ---\n${source.content}`;
  }).join('\n\n');
  
  return `【知识库相关内容】\n\n${knowledgeContent}`;
}

/**
 * 纯检索模式（不调用 LLM），返回检索到的文本片段
 * 适用于需要自行处理检索结果的场景
 */
export async function ragSearch(
  query: string,
  topK: number = 5
): Promise<RAGSource[]> {
  const { sources } = await ragRetrieve(query, topK);
  return sources;
}

/**
 * 获取知识库概览信息
 */
export async function getKnowledgeOverview(): Promise<{
  totalDocs: number;
  totalChunks: number;
  topDocuments: { fileName: string; hitCount: number }[];
}> {
  const knowledgeStore = useKnowledgeStore();
  const { getStats } = await import('../lib/vectorDB');

  const stats = await getStats();

  // Top 5 最常被引用的文档
  const topDocs = knowledgeStore.documents
    .filter(d => d.hitCount > 0)
    .sort((a, b) => b.hitCount - a.hitCount)
    .slice(0, 5)
    .map(d => ({
      fileName: d.fileName,
      hitCount: d.hitCount,
    }));

  return {
    totalDocs: stats.totalDocs,
    totalChunks: stats.totalChunks,
    topDocuments: topDocs,
  };
}
