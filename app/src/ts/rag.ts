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
 * 默认系统提示词
 */
const DEFAULT_SYSTEM_PROMPT = `你是一个量化交易助手，基于以下知识库中的文档内容回答用户的问题。
请仅使用提供的上下文信息作答，如果上下文信息不足以回答问题，请明确告知用户。
回答时请标明信息来源出自哪个文档。`;

/**
 * 组装 RAG Prompt
 */
function buildPrompt(query: string, sources: RAGSource[], systemPrompt: string): { prompt: string; context: string } {
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

请基于以上知识库上下文回答用户的问题。`;

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
 * @param topK 检索文档块数量
 * @param systemPrompt 自定义系统提示词
 * @param llmCallback LLM 调用回调函数（由调用方提供具体的 LLM API 调用逻辑）
 * @returns RAG 完整响应
 */
export async function ragQuery(
  query: string,
  llmCallback: (prompt: string) => Promise<string>,
  topK: number = 5,
  systemPrompt: string = DEFAULT_SYSTEM_PROMPT
): Promise<RAGResponse> {
  // 1. 检索阶段
  const { sources, context } = await ragRetrieve(query, topK);

  if (sources.length === 0) {
    // 没有检索到相关内容
    return {
      answer: '抱歉，知识库中没有找到与您问题相关的内容。请尝试上传相关文档后再提问。',
      sources: [],
      context: '',
    };
  }

  // 2. 组装 Prompt
  const { prompt } = buildPrompt(query, sources, systemPrompt);

  // 3. 调用 LLM
  const answer = await llmCallback(prompt);

  return {
    answer,
    sources,
    context,
  };
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
