/**
 * 统一检索工具（渐进式披露的入口）
 *
 * 为什么放在 agent 层而不是 src/lib/tools/：
 *   tools/* → agent/toolCatalog → tools/* 会成循环依赖；
 *   agent 层 → tools 层是单向的。
 *
 * 四个 action：
 * - search  同时检索工具目录与 skills，分词匹配，返回紧凑候选
 * - enable  把某个工具加进本次会话的绑定集合（下一轮 LLM 就能看到它的完整 schema）
 * - read    读取 skill 正文（等价于旧 skill 工具的 read）
 * - list    列出本 Agent 可用的全部工具名与技能名
 *
 * 为什么 enable 只回一句短确认：
 *   完整 schema 会由下一轮的 tools 数组提供，这里再吐一遍纯属重复烧 token。
 */

import { tool } from "@langchain/core/tools"
import type { StructuredToolInterface } from "@langchain/core/tools"
import { z } from "zod"
import { tokenizeQuery, type ToolCatalog, type ToolCatalogEntry } from "./toolCatalog"

export interface SearchToolDeps {
  /** 工具目录 */
  catalog: ToolCatalog
  /** 本 Agent 可启用的工具白名单 */
  allowedTools: string[]
  /**
   * 常驻的转交工具名（ask_xxx）
   *
   * 检索结果里会提示「这类问题直接交给对应 Agent」——
   * 否则 strategy Agent 搜「风控」会拿到一个它启用了也没用的结果。
   */
  residentToolNames?: string[]
  /** 启用回调：把工具加入本次 agent run 的绑定集合 */
  onEnable: (name: string) => void
}

/** 转交工具 → 领域关键词（用于检索结果里的「直接转交」提示） */
const TRANSFER_HINTS: Array<{ name: string; label: string; aliases: string[] }> = [
  { name: "ask_risk", label: "Risk Agent：风控、止损、敞口、保证金", aliases: ["风控", "风险", "止损", "敞口", "保证金", "VaR"] },
  { name: "ask_portfolio", label: "Portfolio Agent：组合收益、资产配置、调仓", aliases: ["组合", "资产配置", "调仓", "再平衡"] },
  { name: "ask_strategy", label: "Strategy Agent：策略创建、回测、策略图", aliases: ["策略", "回测", "策略图", "信号", "持仓策略"] },
  { name: "ask_chat", label: "Chat Agent：知识问答、联网搜索、时间", aliases: ["知识", "问答", "联网", "新闻", "时间", "日期"] },
]

/** 检索结果里最多显示几个候选 */
const MAX_RESULTS = 6
/** 检索结果里每个工具最多列几个 action */
const MAX_ACTIONS = 8
/** search 结果里最多显示几个技能 */
const MAX_SKILLS = 3

/** 读取 skills（非 Electron 环境返回 null） */
async function loadSkills(): Promise<SkillInfo[] | null> {
  if (typeof window === "undefined") return null
  const api = window.skillsAPI
  if (!api) return null
  try {
    const res = await api.list()
    return res?.success ? res.skills : null
  } catch {
    return null
  }
}

/**
 * 技能检索
 *
 * 旧实现是整串 includes —— Agent 传「蒙特卡洛 风险」这种多词短语必然零命中。
 * 这里复用工具目录的分词器，任一 token 命中即收录。
 */
function matchSkills(skills: SkillInfo[], query: string) {
  const segments = tokenizeQuery(query)
  if (!segments.length) return []

  const results: Array<{ skill: SkillInfo; score: number; matched: string[] }> = []
  for (const skill of skills) {
    const name = skill.name.toLowerCase()
    const desc = skill.description.toLowerCase()
    const keywords = skill.keywords.map((k) => k.toLowerCase())

    let score = 0
    const matched = new Set<string>()
    for (const seg of segments) {
      if (name.includes(seg)) {
        score += 40
        matched.add(seg)
      } else if (keywords.some((k) => k.includes(seg))) {
        score += 30
        matched.add(seg)
      } else if (desc.includes(seg)) {
        score += 15
        matched.add(seg)
      }
    }
    if (score > 0) results.push({ skill, score, matched: [...matched] })
  }

  results.sort((a, b) => b.score - a.score)
  return results.slice(0, MAX_SKILLS)
}

/**
 * 转交工具提示
 *
 * 命中的是常驻 ask_xxx 时，直接告诉模型「这类问题交给对应 Agent」，
 * 比让它反复 enable 一个不在白名单里的工具更省事。
 */
function matchTransferHints(query: string, resident: Set<string>): string[] {
  if (!resident.size) return []
  const segments = tokenizeQuery(query)
  if (!segments.length) return []

  const hits: string[] = []
  for (const hint of TRANSFER_HINTS) {
    if (!resident.has(hint.name)) continue
    const matched = segments.some((seg) =>
      hint.aliases.some((a) => a.toLowerCase().includes(seg) || seg.includes(a.toLowerCase())),
    )
    if (matched) hits.push(`- **${hint.name}**（常驻，无需启用）— ${hint.label}`)
  }
  return hits
}

/** 一条工具的紧凑展示：名称 + 摘要 + 命中原因 + 与命中相关的 action */
function formatEntry(entry: ToolCatalogEntry, matched?: string[], matchedActions?: string[]): string {
  const parts = [`- **${entry.name}** — ${entry.summary || "(无描述)"}`]

  const hints: string[] = []
  if (matched?.length) hints.push(`命中：${matched.slice(0, 4).join("、")}`)

  // 优先展示与查询相关的 action，其次按声明顺序
  let actions = entry.actions
  if (matchedActions?.length) {
    const hit = new Set(matchedActions)
    actions = [...matchedActions, ...entry.actions.filter((a) => !hit.has(a))]
  }
  if (actions.length) {
    const shown = actions.slice(0, MAX_ACTIONS)
    const suffix = actions.length > shown.length ? ` 等 ${actions.length} 个` : ""
    hints.push(`操作：${shown.join(", ")}${suffix}`)
  }
  if (hints.length) parts.push(`  （${hints.join("；")}）`)

  return parts.join("\n")
}

export function createSearchTool(deps: SearchToolDeps): StructuredToolInterface {
  const { catalog, allowedTools, residentToolNames = [], onEnable } = deps
  const allowed = new Set(allowedTools)
  const resident = new Set(residentToolNames)

  return tool(
    async ({ action, keyword }) => {
      switch (action) {
        /* ---------- 检索 ---------- */
        case "search": {
          if (!keyword?.trim()) {
            return '错误：search 需要 keyword 参数。多个关键词用空格分隔，例如 keyword="行情 下载"。'
          }

          const matches = catalog.search(keyword, MAX_RESULTS, allowed)
          const hints = matchTransferHints(keyword, resident)
          const skills = await loadSkills()
          const skillMatches = skills ? matchSkills(skills, keyword) : []

          if (matches.length === 0 && skillMatches.length === 0 && hints.length === 0) {
            const available = catalog.list().filter((e) => allowed.has(e.name)).map((e) => e.name)
            return [
              `未找到与「${keyword}」相关的能力。`,
              ``,
              `本 Agent 可启用的工具（${available.length} 个）：${available.join(", ")}`,
              `可以换个说法再搜，或直接 enable 上面某个工具。`,
            ].join("\n")
          }

          const lines: string[] = [`【能力检索】「${keyword}」命中 ${matches.length} 个工具${skillMatches.length ? `、${skillMatches.length} 个技能` : ""}`]

          if (matches.length) {
            lines.push("", "**工具**")
            for (const m of matches) {
              const hitActions = m.entry.actions.filter((a) =>
                m.matched.some((seg) => a.toLowerCase().includes(seg.toLowerCase())),
              )
              lines.push(formatEntry(m.entry, m.matched, hitActions))
            }
            lines.push(
              "",
              '下一步：对选中的工具调用 search(action="enable", keyword="<工具名>")。',
              "启用后它的完整参数说明会自动出现在你的工具列表里，直接调用即可。",
            )
          }

          if (hints.length) {
            lines.push("", "**转交工具**（已常驻，直接调用即可，不用 enable）")
            lines.push(...hints)
          }

          if (skillMatches.length) {
            lines.push("", "**技能**（领域工作流/最佳实践）")
            for (const s of skillMatches) {
              lines.push(`- **${s.skill.name}** (\`${s.skill.id}\`) — ${s.skill.description}`)
            }
            lines.push('', '加载技能正文：search(action="read", keyword="<技能 ID>")。')
          }

          return lines.join("\n")
        }

        /* ---------- 启用 ---------- */
        case "enable": {
          const name = keyword?.trim()
          if (!name) return "错误：enable 需要 keyword 参数（要启用的工具名）。"

          const entry = catalog.get(name)
          if (!entry) {
            const available = catalog.list().map((e) => e.name)
            return [
              `工具目录里没有「${name}」。`,
              `目录里全部工具：${available.join(", ")}。`,
              `先用 search 确认正确名称。`,
            ].join("\n")
          }

          if (!allowed.has(name)) {
            const available = catalog.list().filter((e) => allowed.has(e.name)).map((e) => e.name)
            return [
              `「${name}」不在本 Agent 的可启用范围内。`,
              `可启用的工具：${available.join(", ")}。`,
              `如果这属于别的 Agent 的职责，用对应的 ask_xxx 转交工具。`,
            ].join("\n")
          }

          onEnable(name)

          const actions = entry.actions.length
            ? `\n可用 action：${entry.actions.join(", ")}`
            : ""
          return `已启用 **${entry.name}** — ${entry.summary}${actions}\n参数说明已加载，现在可以直接调用 ${entry.name}。`
        }

        /* ---------- 读技能 ---------- */
        case "read": {
          const skillId = keyword?.trim()
          if (!skillId) return "错误：read 需要 keyword 参数（技能 ID）。"

          if (typeof window === "undefined" || !window.skillsAPI) {
            return "【技能】技能系统不可用（非 Electron 环境）。工具检索与启用不受影响。"
          }

          const res = await window.skillsAPI.read(skillId)
          if (!res.success) {
            return `【技能】"${skillId}" 不存在或读取失败：${res.error || "未知错误"}`
          }
          return `【技能: ${res.name}】\n${res.description}\n\n${res.content}`
        }

        /* ---------- 列目录 ---------- */
        case "list": {
          const entries = catalog.list().filter((e) => allowed.has(e.name))
          const lines = [`【本 Agent 可启用的能力】共 ${entries.length} 个工具：`]
          for (const e of entries) {
            lines.push(`- **${e.name}** — ${e.summary || "(无描述)"}`)
          }

          const skills = await loadSkills()
          if (skills?.length) {
            lines.push("", `**技能**（${skills.length} 个）`)
            for (const s of skills) {
              lines.push(`- **${s.name}** (\`${s.id}\`) — ${s.description}`)
            }
          } else if (skills === null) {
            lines.push("", "（技能系统不可用，仅工具可检索）")
          }

          lines.push("", '用 search(action="enable", keyword="<工具名>") 启用需要的工具。')
          return lines.join("\n")
        }

        default:
          return `未知 action: ${action}。支持: search, enable, read, list`
      }
    },
    {
      name: "search",
      description:
        "能力检索与按需启用。你当前只绑定了本工具和少量转交工具，其余能力要「先检索、再启用」才能调用。\n" +
        "\n用法：\n" +
        '1) action="search" keyword="行情 下载" —— 检索工具与技能；多个关键词用空格分隔。\n' +
        '2) action="enable" keyword="data_manager" —— 启用工具；启用后它的完整参数说明会自动进入你的工具列表，直接调用即可。\n' +
        '3) action="read" keyword="<技能ID>" —— 读取技能正文（领域工作流、最佳实践、约束规则）。\n' +
        '4) action="list" —— 列出本 Agent 可用的全部工具与技能。\n' +
        "\n可检索的能力域：行情与数据下载、财务数据、策略图与回测、风控与止损、绩效指标、持仓与资金、订单成交、期权定价、统计分析（EMD/协整/CUSUM/PCA/波动率/突变）、图表与可视化、知识库检索、联网搜索、日期时间、系统信息、计算器。\n" +
        "\n注意：直接调用尚未启用的工具会失败，必须先 enable。",
      schema: z.object({
        action: z.enum(["search", "enable", "read", "list"]).describe("操作类型：search 检索 / enable 启用 / read 读技能 / list 列全部"),
        keyword: z
          .string()
          .optional()
          .describe("search=关键词（空格分隔多个）；enable=工具名；read=技能 ID"),
      }),
    },
  )
}
