/**
 * LanceDB Retriever
 * 将知识库向量检索封装为 LangChain Retriever 接口
 *
 * 注意：此模块保留用于未来需要复杂 RAG 流程时使用。
 * 当前 knowledge Tool 直接调用 vectorSearch，更简单高效。
 */

import { BaseRetriever } from "@langchain/core/retrievers"
import { Document } from "@langchain/core/documents"
import { search as vectorSearch } from "./vectorDB"

export class LanceDBRetriever extends BaseRetriever {
  lc_namespace = ["quasarx", "retriever"]

  private topK: number

  constructor(options?: { topK?: number }) {
    super()
    this.topK = options?.topK ?? 5
  }

  async _getRelevantDocuments(query: string): Promise<Document[]> {
    const results = await vectorSearch(query, this.topK)
    return results.map(r =>
      new Document({
        pageContent: r.summary
          ? `【摘要】${r.summary}\n\n${r.chunks.map(c => c.content).join('\n\n')}`
          : r.chunks.map(c => c.content).join('\n\n'),
        metadata: {
          fileName: r.fileName,
          docId: r.docId,
          similarity: r.similarity,
          chunkCount: r.chunks.length,
        },
      })
    )
  }
}
