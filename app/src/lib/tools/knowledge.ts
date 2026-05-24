/**
 * 知识库检索 Tool
 * 检索用户上传的本地知识库文档
 *
 * 使用摘要索引模式：返回摘要 + 完整 chunks
 */

import { tool } from "@langchain/core/tools"
import { z } from "zod"
import { search as vectorSearch } from "@/lib/vectorDB"
import { useKnowledgeStore } from "@/stores/knowledgeStore"

export const knowledgeTool = tool(
  async ({ query, topK, tags }) => {
    try {
      const results = await vectorSearch(query, topK, tags)

      if (results.length === 0) {
        return "【知识库】未找到与问题相关的文档内容。"
      }

      // 更新命中次数
      const knowledgeStore = useKnowledgeStore()

      // 构建返回文本
      const docs = results.map((r, idx) => {
        knowledgeStore.incrementHitCount(r.docId)

        const fullContent = r.chunks.map((c) => c.content).join("\n\n")
        const tagsStr = r.tags && r.tags.length > 0 ? ` [${r.tags.join(", ")}]` : ""

        return `--- 文档 ${idx + 1}: ${r.fileName}${tagsStr} (相关度: ${(r.similarity * 100).toFixed(1)}%) ---
【摘要】${r.summary}

【正文】
${fullContent}`
      }).join("\n\n")

      return `【知识库相关内容】\n\n${docs}`
    } catch (e) {
      console.warn("[Knowledge Tool] 检索失败:", e)
      return "【知识库】检索服务暂时不可用。"
    }
  },
  {
    name: "knowledge",
    description: "检索本地知识库文档，查找与用户问题相关的专业知识。适用于金融、量化交易等专业领域问题。返回内容包含摘要和正文。可按标签过滤文档，如 ['财务', '年报']。",
    schema: z.object({
      query: z.string().describe("检索关键词，通常是用户的核心问题"),
      topK: z.number().default(5).describe("返回结果数量，默认 5"),
      tags: z.array(z.string()).optional().describe("可选：按标签过滤，如 ['财务', '年报']。匹配标签的文档会获得更高的排序优先级"),
    }),
  }
)
