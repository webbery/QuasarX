/**
 * 知识库检索 Tool
 * 检索用户上传的本地知识库文档
 */

import { tool } from "@langchain/core/tools"
import { z } from "zod"
import { search as vectorSearch } from "@/lib/vectorDB"
import { useKnowledgeStore } from "@/stores/knowledgeStore"

export const knowledgeTool = tool(
  async ({ query, topK }) => {
    try {
      const results = await vectorSearch(query, topK)

      if (results.length === 0) {
        return "【知识库】未找到与问题相关的文档内容。"
      }

      // 更新命中次数
      const knowledgeStore = useKnowledgeStore()
      const hitDocIds = new Set<string>()
      for (const result of results) {
        if (!hitDocIds.has(result.chunk.docId)) {
          hitDocIds.add(result.chunk.docId)
          knowledgeStore.incrementHitCount(result.chunk.docId)
        }
      }

      // 构建返回文本
      const docs = results.map((r, idx) => {
        return `--- 文档 ${idx + 1}: ${r.chunk.fileName} (相似度: ${(r.similarity * 100).toFixed(1)}%) ---\n${r.chunk.content}`
      }).join("\n\n")

      return `【知识库相关内容】\n\n${docs}`
    } catch (e) {
      console.warn("[Knowledge Tool] 检索失败:", e)
      return "【知识库】检索服务暂时不可用。"
    }
  },
  {
    name: "knowledge",
    description: "检索本地知识库文档，查找与用户问题相关的专业知识。适用于金融、量化交易等专业领域问题。",
    schema: z.object({
      query: z.string().describe("检索关键词，通常是用户的核心问题"),
      topK: z.number().default(5).describe("返回结果数量，默认 5"),
    }),
  }
)
