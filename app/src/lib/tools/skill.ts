/**
 * Skill Tool
 *
 * Agent 通过此工具发现并加载第三方 Skill（能力插件）。
 * Skill 以 SKILL.md 文件形式存储在 skills/ 目录，
 * Agent 读取后获得对应的领域知识和行为指令。
 */

import { tool } from "@langchain/core/tools"
import { z } from "zod"

export const skillTool = tool(
  async ({ action, keyword }) => {
    const api = window.skillsAPI
    if (!api) return "【Skill】技能系统不可用（非 Electron 环境）"

    switch (action) {
      case "list": {
        const res = await api.list()
        if (!res.success || res.skills.length === 0)
          return "【Skill】当前没有可用的技能。"

        const lines = res.skills.map(
          (s: SkillInfo) =>
            `- **${s.name}** (\`${s.id}\`) — ${s.description}${s.keywords.length ? ` [${s.keywords.join(", ")}]` : ""}`,
        )
        return `【可用技能】共 ${res.skills.length} 个：\n${lines.join("\n")}\n\n使用 action="read" 并传入 skill_id 来加载技能内容。`
      }

      case "search": {
        if (!keyword) return "错误：search 需要提供 keyword 参数"

        const res = await api.list()
        if (!res.success) return "【Skill】技能列表获取失败"

        const q = keyword.toLowerCase()
        const matched = res.skills.filter(
          (s: SkillInfo) =>
            s.name.toLowerCase().includes(q) ||
            s.description.toLowerCase().includes(q) ||
            s.keywords.some((k: string) => k.toLowerCase().includes(q)),
        )

        if (matched.length === 0)
          return `【Skill】未找到与 "${keyword}" 相关的技能。`

        const lines = matched.map(
          (s: SkillInfo) =>
            `- **${s.name}** (\`${s.id}\`) — ${s.description}`,
        )
        return `【技能搜索结果】匹配 "${keyword}" 的技能（${matched.length} 个）：\n${lines.join("\n")}\n\n使用 action="read" 加载具体技能。`
      }

      case "read": {
        if (!keyword) return "错误：read 需要提供 keyword 参数（skill_id）"

        const res = await api.read(keyword)
        if (!res.success)
          return `【Skill】技能 "${keyword}" 不存在或读取失败：${res.error || "未知错误"}`

        return `【技能: ${res.name}】\n${res.description}\n\n${res.content}`
      }

      default:
        return `未知 action: ${action}。支持: list, search, read`
    }
  },
  {
    name: "skill",
    description:
      "加载第三方技能（Skill）以获取特定领域的知识和行为指令。" +
      "技能包含工作流、参数建议、约束规则等，读取后应严格遵循其中的指导。" +
      "\n\n使用场景：\n" +
      "- 用户询问特定领域问题时，先搜索相关技能获取专业指导\n" +
      "- 执行复杂任务前，加载对应技能了解最佳实践和约束\n" +
      "- 不确定某个功能怎么用，读取技能获取详细说明",
    schema: z.object({
      action: z
        .enum(["list", "search", "read"])
        .describe("操作类型：list=列出所有技能, search=按关键词搜索, read=读取技能内容"),
      keyword: z
        .string()
        .optional()
        .describe("搜索关键词（search 时必填）或技能 ID（read 时必填）"),
    }),
  },
)
