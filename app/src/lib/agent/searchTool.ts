/**
 * 统一检索工具（渐进式披露的入口）
 *
 * 为什么放在 agent 层而不是 src/lib/tools/：
 *   tools/* → agent/toolCatalog → tools/* 会成循环依赖；
 *   agent 层 → tools 层是单向的。
 *
 * 五个 action（两级加载）：
 * - domain   看能力域：场景、限制、域内工具与动作清单（不含 JSON Schema）
 * - contract 加载某个工具的完整契约（description + 参数说明）并绑定它
 * - search   关键词模糊检索（兜底：域路由失败时的第二条路）
 * - read     读取 skill 正文（等价于旧 skill 工具的 read）
 * - list     列出本 Agent 可用的全部能力域与技能
 *
 * 为什么 contract 合并了 enable：
 *   「启用」和「看参数说明」本来就是一件事 —— 模型拿到工具名却不知道参数，
 *   只能靠猜。合并后模型一次调用就同时拿到契约和绑定结果。
 */

import { tool } from "@langchain/core/tools"
import type { StructuredToolInterface } from "@langchain/core/tools"
import { z } from "zod"
import { tokenizeQuery, type ToolCatalog, type ToolCatalogEntry } from "./toolCatalog"
import {
  capabilityOwner,
  domainsForAgent,
  effectiveTools,
  type CapabilityDomain,
} from "./capabilityMap"
import type { AgentType } from "./types"

export interface SearchToolDeps {
  /** 工具目录 */
  catalog: ToolCatalog
  /** 本 Agent 可启用的工具白名单 */
  allowedTools: string[]
  /** 当前 Agent（决定能力地图的可见范围） */
  agentType: AgentType
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
/** 契约正文上限（避免把超长 description 全量灌进上下文） */
const MAX_CONTRACT_CHARS = 2400

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

/** 域的模糊匹配：域 id 精确命中 > 标题/场景/关键词分词命中 */
function matchDomain(query: string, domains: CapabilityDomain[]): CapabilityDomain | undefined {
  const q = (query ?? "").trim().toLowerCase()
  if (!q) return undefined

  const exact = domains.find((d) => d.id.toLowerCase() === q)
  if (exact) return exact

  const segments = tokenizeQuery(query)
  let best: { domain: CapabilityDomain; score: number } | undefined
  for (const d of domains) {
    const hay = [d.id, d.title, d.scenarios, ...(d.keywords ?? [])].join(" ").toLowerCase()
    let score = 0
    for (const seg of segments) {
      if (d.id.toLowerCase().includes(seg)) score += 40
      else if (hay.includes(seg)) score += 18
    }
    if (score > 0 && (!best || score > best.score)) best = { domain: d, score }
  }
  return best?.domain
}

export function createSearchTool(deps: SearchToolDeps): StructuredToolInterface {
  const { catalog, allowedTools, agentType, residentToolNames = [], onEnable } = deps
  const allowed = new Set(allowedTools)
  const resident = new Set(residentToolNames)
  const domains = domainsForAgent(agentType).filter((d) => effectiveTools(agentType, d, allowed).length > 0)

  /** 域卡片：域内工具 + 各自的动作清单（不含 JSON Schema） */
  function formatDomain(domain: CapabilityDomain): string {
    const lines = [`**${domain.id}** · ${domain.title}`, `适用：${domain.scenarios}`]
    if (domain.limits.length) lines.push(`限制：${domain.limits.join("；")}`)
    if (domain.requires?.length) lines.push(`前置：${domain.requires.join("；")}`)
    const tools = effectiveTools(agentType, domain, allowed)
    lines.push("域内工具：")
    for (const name of tools) {
      const entry = catalog.get(name)
      if (!entry) {
        // ui 这类常驻工具不进目录（不需要契约加载）
        lines.push(`- ${name}（常驻，无需加载契约）`)
        continue
      }
      const actions = entry.actions.length ? ` → ${entry.actions.slice(0, MAX_ACTIONS).join(", ")}` : ""
      lines.push(`- ${name}${actions}`)
    }
    const gaps = (domain.notCovered ?? []).filter((nc) => !nc.routeTo)
    if (gaps.length) {
      lines.push(`不覆盖：${gaps.map((g) => g.topic).join("；")}（这类需求用 report_gap 登记）`)
    }
    return lines.join("\n")
  }

  /** 越权提示：这个工具归谁管 */
  function ownershipHint(toolName: string): string {
    const owner = capabilityOwner(toolName)
    if (!owner.domains.length) {
      return `「${toolName}」不在系统的任何能力域内 —— 系统没有这个能力，不要用相近工具硬凑。`
    }
    const domainsText = owner.domains.map((d) => d.id).join("、")
    if (owner.routeTo) {
      return `「${toolName}」属于 ${domainsText} 域，不在当前 Agent 的能力域内 —— 用 \`ask_${owner.routeTo}\` 转交给 ${owner.routeTo} Agent。`
    }
    return `「${toolName}」属于 ${domainsText} 域，不在当前 Agent 的能力域内。`
  }

  return tool(
    async ({ action, keyword }) => {
      switch (action) {
        /* ---------- 能力域 ---------- */
        case "domain": {
          if (!keyword?.trim()) {
            return '错误：domain 需要 keyword 参数。填域 id（如 "market_data"）或描述你要做的事（如 "下载行情"）。'
          }

          const domain = matchDomain(keyword, domains)
          if (!domain) {
            return [
              `没有匹配「${keyword}」的能力域。`,
              ``,
              `本 Agent 可用的能力域：`,
              ...domains.map((d) => `- ${d.id} · ${d.title}（${d.scenarios}）`),
              ``,
              `换个说法再试；如果确实没有这个能力，用 report_gap 登记缺口，不要用相近工具硬凑。`,
            ].join("\n")
          }

          return [
            `【能力域】${domain.id} · ${domain.title}`,
            formatDomain(domain),
            ``,
            `下一步：search(action="contract", keyword="<工具名>") 加载该工具的完整契约，加载后下一轮即可调用。`,
          ].join("\n")
        }

        /* ---------- 工具契约 ---------- */
        case "contract":
        case "enable": {
          const name = keyword?.trim()
          if (!name) return "错误：contract 需要 keyword 参数（要加载的工具名）。"

          const entry = catalog.get(name)
          if (!entry) {
            const available = catalog.list().filter((e) => allowed.has(e.name)).map((e) => e.name)
            return [
              `工具目录里没有「${name}」。`,
              `本 Agent 可用的工具：${available.join(", ")}。`,
              `先 search(action="domain") 找到对应能力域，或换用正确的工具名。`,
            ].join("\n")
          }

          if (!allowed.has(name)) {
            return [
              `「${name}」不在当前 Agent 的能力域内。`,
              ownershipHint(name),
              ``,
              `本 Agent 可用的能力域：${domains.map((d) => d.id).join(", ")}。`,
              `如果这属于别的 Agent 的职责，用对应的 ask_xxx 转交工具。`,
            ].join("\n")
          }

          onEnable(name)

          const actions = entry.actions.length ? `\n可用 action：${entry.actions.join(", ")}` : ""
          const params = entry.params ? `\n\n参数说明：\n${entry.params}` : ""
          const desc = entry.description.length > MAX_CONTRACT_CHARS
            ? `${entry.description.slice(0, MAX_CONTRACT_CHARS)}…（已截断，完整行为以工具校验为准）`
            : entry.description
          return [
            `已加载 **${entry.name}** 的契约，下一轮即可直接调用。`,
            actions,
            ``,
            desc,
            params,
          ].join("\n")
        }

        /* ---------- 检索（兜底） ---------- */
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
              `本 Agent 可用的工具（${available.length} 个）：${available.join(", ")}`,
              `本 Agent 可用的能力域：${domains.map((d) => d.id).join(", ")}`,
              `可以换个说法再搜，或改用 search(action="domain") 从能力域切入。`,
              `如果确实没有这个能力，用 report_gap 登记缺口。`,
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
              '下一步：对选中的工具调用 search(action="contract", keyword="<工具名>") 加载契约。',
              "加载后它的完整参数说明会自动出现在你的工具列表里，直接调用即可。",
            )
          }

          if (hints.length) {
            lines.push("", "**转交工具**（已常驻，直接调用即可，不用加载）")
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

        /* ---------- 读技能 ---------- */
        case "read": {
          const skillId = keyword?.trim()
          if (!skillId) return "错误：read 需要 keyword 参数（技能 ID）。"

          if (typeof window === "undefined" || !window.skillsAPI) {
            return "【技能】技能系统不可用（非 Electron 环境）。工具检索与加载不受影响。"
          }

          const res = await window.skillsAPI.read(skillId)
          if (!res.success) {
            return `【技能】"${skillId}" 不存在或读取失败：${res.error || "未知错误"}`
          }
          return `【技能: ${res.name}】\n${res.description}\n\n${res.content}`
        }

        /* ---------- 列目录 ---------- */
        case "list": {
          const lines = [`【本 Agent 可用的能力域】共 ${domains.length} 个：`]
          for (const d of domains) {
            const tools = effectiveTools(agentType, d, allowed)
            lines.push(`- **${d.id}** · ${d.title} — ${d.scenarios}`)
            lines.push(`  工具：${tools.join(", ")}`)
          }

          const skills = await loadSkills()
          if (skills?.length) {
            lines.push("", `**技能**（${skills.length} 个）`)
            for (const s of skills) {
              lines.push(`- ${s.name} (\`${s.id}\`) — ${s.description}`)
            }
          } else if (skills === null) {
            lines.push("", "（技能系统不可用，仅工具可检索）")
          }

          lines.push("", '加载工具契约：search(action="contract", keyword="<工具名>")。')
          return lines.join("\n")
        }

        default:
          return `未知 action: ${action}。支持: domain, contract, search, read, list`
      }
    },
    {
      name: "search",
      description:
        "能力检索与按需加载。你当前只绑定了本工具和少量转交工具，其余能力要「先取域、再加载契约」才能调用。\n" +
        "\n用法：\n" +
        '1) action="domain" keyword="行情" —— 按能力域检索（场景、限制、域内工具与动作）。域 id 见下。\n' +
        '2) action="contract" keyword="data_manager" —— 加载工具契约；加载后它的完整参数说明自动进入你的工具列表，下一轮即可调用。\n' +
        '3) action="search" keyword="下载 行情" —— 关键词模糊检索（域路由失败时的兜底）。\n' +
        '4) action="read" keyword="<技能ID>" —— 读取技能正文（领域工作流、最佳实践、约束规则）。\n' +
        '5) action="list" —— 列出本 Agent 可用的全部能力域与技能。\n' +
        "\n可用能力域：market_data 行情与基础数据、strategy_backtest 策略与回测、quant_analytics 统计与计量分析、" +
        "option_derivatives 期权与衍生品、risk_monitoring 风控指标、account_funds 账户资金、position_holdings 持仓、" +
        "order_history 订单流水、knowledge_web 知识库与联网、render_ui 图表与交互展示、script_exec 脚本执行（计算/时间/系统信息）。" +
        "\n注意：直接调用尚未加载契约的工具会被拒绝并要求先加载。",
      schema: z.object({
        action: z.enum(["domain", "contract", "enable", "search", "read", "list"]).describe("操作类型：domain 取能力域 / contract 加载工具契约（enable 是它的旧别名）/ search 模糊检索 / read 读技能 / list 列全部"),
        keyword: z
          .string()
          .optional()
          .describe("domain=域 id 或要做的事；contract=工具名；search=关键词（空格分隔多个）；read=技能 ID"),
      }),
    },
  )
}
