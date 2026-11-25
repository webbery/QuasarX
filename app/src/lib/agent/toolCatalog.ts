/**
 * 工具目录（渐进式披露）
 *
 * 设计要点：
 * 1. 目录从**真实 Tool 实例**派生（getAllTools()），不手写 description 副本 ——
 *    手写副本必然与 src/lib/tools/*.ts 漂移，改了 description 忘同步目录，
 *    Agent 看到的说明和实际行为就会不一致，比硬编码更难 debug。
 * 2. 中文别名表是召回率的关键：Tool 的 description 已是中文，但 action 名是英文
 *    （download_quote / list_strategies…），用户 query 又是中文口语，
 *    纯英文匹配必然零命中。
 * 3. 匹配用「分词 + 子串」而非「整串 includes」：
 *    「下载 行情 数据」这类多词短语按整串匹配必然搜不到（老 skill 工具的 bug）；
 *    「下载600584.SH行情数据」这种无空格中文串靠二元组分词才能命中。
 *
 * 目录条目同时服务两处：
 * - 检索结果：只给 summary + 命中原因 + action 列表，几百字符
 * - 启用之后：完整 description + schema 由绑定的 tools 数组提供，不在这里重复
 */

import { toJsonSchema } from "@langchain/core/utils/json_schema"
import type { StructuredToolInterface } from "@langchain/core/tools"
import { getAllTools } from "../tools"
import { verifyCapabilityMap } from "./capabilityMap"
import { AGENT_TOOL_CONFIG } from "./types"

/**
 * 中文别名表（手写静态）
 *
 * key 必须与真实 Tool 名一致（src/lib/tools/*.ts 里的 name 字段）。
 * 改了 Tool 名字要同步改这里 —— 构建目录时会对不上的 key 打告警。
 */
const TOOL_ALIASES: Record<string, string[]> = {
  data_manager: ["行情", "数据下载", "下载数据", "财务", "股票列表", "期权", "期货", "数据管理", "导入", "导出", "K线"],
  analysis: ["波动率", "pca", "分解", "信号", "蒙特卡洛", "突变", "变点", "变点检测", "cusum", "emd", "xgboost", "协方差", "矩阵", "特征值", "正定性"],
  time_series: ["协整", "单位根", "ADF", "KPSS", "平稳性", "平稳", "迹检验", "最大特征值", "Johansen", "Granger", "因果", "时序", "时间序列", "半衰期", "配对", "Engle-Granger"],
  risk_monitor: ["风控", "风险", "止损", "资本", "敞口", "断路器"],
  order_query: ["订单", "成交", "交易查询", "资金流水"],
  performance: ["绩效", "收益", "业绩", "年化", "夏普"],
  strategy: ["策略", "策略图", "节点", "回测", "参数", "信号语法", "公式语法", "均线", "技术指标", "指标"],
  backtest: ["回测", "绩效指标", "最大回撤", "策略回测"],
  quote: ["实时行情", "报价", "最新价", "历史行情"],
  option_pricing: ["期权", "定价", "希腊", "隐波", "IV", "波动率曲面", "期权收益", "组合收益", "盈亏", "盈亏平衡"],
  account: ["账户", "资金", "杠杆", "保证金"],
  position: ["持仓", "仓位", "持仓结构", "集中度"],
  javascript: ["计算", "脚本", "百分比", "复利", "单利", "标准差", "均值", "方差", "日期", "时间", "今天", "几点", "系统环境", "操作系统", "平台"],
  knowledge: ["知识库", "文档", "检索"],
  web_search: ["联网", "搜索", "新闻", "网络"],
}

/**
 * 不进入目录的 Tool
 *
 * - skill：能力检索已由 agent 层的 search 工具统一承担，而 skill 自带的 search 是
 *   整串 includes 匹配（多词短语搜不到），留着只会误导；它的 Tool 定义仍保留，
 *   供 getAllTools() 直接取用。
 * - ui：常驻工具，每个 Agent 都已绑定，不需要检索/启用。
 */
const EXCLUDED_TOOLS = new Set(["skill", "ui"])

/** 目录条目 */
export interface ToolCatalogEntry {
  /** 真实 Tool 名（与 Tool 实例的 name 一致） */
  name: string
  /** 首句摘要（≤120 字），检索结果里显示这个 */
  summary: string
  /** 完整描述（启用后由 tools 数组提供，这里只做检索用） */
  description: string
  /** 精简参数说明，多行 "name: 描述 (类型, 必填/可选)" */
  params: string
  /** schema 里 action 属性的枚举值 */
  actions: string[]
  /** 中文别名 */
  aliases: string[]
  /** 真实 Tool 实例 —— 启用时绑定的就是它 */
  instance: StructuredToolInterface
}

/** 检索命中结果 */
export interface ToolCatalogMatch {
  entry: ToolCatalogEntry
  /** 命中强度，越大越相关 */
  score: number
  /** 命中的查询片段 */
  matched: string[]
  /** 命中落在哪些字段（名称/别名/操作/描述/参数） */
  fields: string[]
}

/** 检索时的字段权重 */
const FIELD_WEIGHT = {
  name: 40,
  alias: 35,
  action: 30,
  description: 18,
  params: 10,
} as const

/** 取描述的首句作为摘要；首句太短则继续拼接，直到够长或到上限 */
function summarize(description: string, limit = 120): string {
  const text = description.replace(/\s+/g, " ").trim()
  if (!text) return ""
  const parts = text.split(/(?<=[。；;!?])\s*/)
  let out = ""
  for (const part of parts) {
    if (out.length + part.length > limit) break
    out += part
    if (out.length >= 24) break
  }
  return (out || text.slice(0, limit)).trim()
}

/** JSON schema 类型 → 短类型名 */
function typeNameOf(schema: any): string {
  if (!schema) return "any"
  const t = schema.type
  if (Array.isArray(t)) return t.join("|")
  if (t === "integer" || t === "number") return "number"
  if (t === "boolean") return "boolean"
  if (t === "array") return "array"
  if (t === "object") return "object"
  return "string"
}

/**
 * 把 JSON schema 压成多行 "name: 描述 (类型, 必填/可选)" 文本
 *
 * 只保留每个属性的 description + 类型 + required 标记，丢掉嵌套结构 ——
 * 嵌套结构在启用后由完整 schema 提供，目录里重复一遍没有价值。
 */
function simplifyParams(schema: any): string {
  const props = schema?.properties
  if (!props || typeof props !== "object") return ""
  const required: string[] = Array.isArray(schema?.required) ? schema.required : []

  const lines: string[] = []
  for (const [key, value] of Object.entries<any>(props)) {
    const desc = typeof value?.description === "string" ? value.description : ""
    const req = required.includes(key) ? "必填" : "可选"
    let line = `${key}: ${desc.slice(0, 70)} (${typeNameOf(value)}, ${req})`
    // action 的枚举单独放在 actions 字段，不在这里重复
    if (key !== "action" && Array.isArray(value?.enum) && value.enum.length <= 12) {
      line += ` [${value.enum.join("|")}]`
    }
    lines.push(line)
  }
  return lines.join("\n")
}

/** 取出 schema 里 action 属性的枚举 */
function extractActions(schema: any): string[] {
  const action = schema?.properties?.action
  return Array.isArray(action?.enum) ? action.enum.map((v: any) => String(v)) : []
}

/**
 * 查询分词
 *
 * 中文没有空格，按整串匹配必然失败（「下载600584.SH行情数据」整串匹配不到任何东西）。
 * 策略：
 * - 连续中文 → 整段 + 二元组（「行情数据」→ 行情/场数/数据）
 * - 连续英文数字 → 整段 + 按 _ - . 拆分（「download_quote」→ download_quote/download/quote）
 * - 过短的片段丢弃（单字中文、两字母英文噪音太大 ——「ma」会命中 data_manager 里的 "ma"）
 */
export function tokenizeQuery(query: string): string[] {
  const q = (query ?? "").toLowerCase()
  const segments = new Set<string>()
  const chunks = q.match(/[㐀-鿿]+|[a-z0-9_+.\-]+/g) ?? []
  for (const chunk of chunks) {
    if (/[a-z]/.test(chunk)) {
      if (chunk.length >= 3) segments.add(chunk)
      for (const part of chunk.split(/[_\-.]+/)) {
        if (part.length >= 3) segments.add(part)
      }
    } else {
      if (chunk.length >= 2) segments.add(chunk)
      for (let i = 0; i + 2 <= chunk.length; i++) {
        segments.add(chunk.slice(i, i + 2))
      }
    }
  }
  return [...segments]
}

/** 工具目录 */
export class ToolCatalog {
  private readonly entries = new Map<string, ToolCatalogEntry>()

  constructor(tools: StructuredToolInterface[]) {
    for (const tool of tools) {
      if (!tool?.name) continue
      if (EXCLUDED_TOOLS.has(tool.name)) continue

      let schema: any = null
      try {
        schema = toJsonSchema(tool.schema)
      } catch (err) {
        console.warn(`[ToolCatalog] 工具 "${tool.name}" 的 schema 解析失败:`, err)
      }

      const description = tool.description ?? ""
      this.entries.set(tool.name, {
        name: tool.name,
        summary: summarize(description),
        description,
        params: simplifyParams(schema),
        actions: extractActions(schema),
        aliases: TOOL_ALIASES[tool.name] ?? [],
        instance: tool,
      })
    }

    this.warnMissingAliases()
  }

  /** 别名表里有、目录里没有 → Tool 改名了，提醒同步 */
  private warnMissingAliases(): void {
    for (const name of Object.keys(TOOL_ALIASES)) {
      if (!this.entries.has(name)) {
        console.warn(`[ToolCatalog] 别名表中的 "${name}" 在工具目录里不存在（Tool 改名了？）`)
      }
    }
  }

  /** 取单个条目 */
  get(name: string): ToolCatalogEntry | undefined {
    return this.entries.get(name)
  }

  /** 全部条目 */
  list(): ToolCatalogEntry[] {
    return [...this.entries.values()]
  }

  /** 目录里是否有名为 name 的工具 */
  has(name: string): boolean {
    return this.entries.has(name)
  }

  /**
   * 按关键词检索工具
   *
   * @param query 查询词（可含多个词，空格/逗号/顿号分隔）
   * @param limit 最多返回几条
   * @param allowed 可用白名单（Agent 只能启用白名单内的工具）
   */
  search(query: string, limit = 6, allowed?: Set<string>): ToolCatalogMatch[] {
    const segments = tokenizeQuery(query)
    if (segments.length === 0) return []

    const normalized = (query ?? "").trim().toLowerCase()
    const matches: ToolCatalogMatch[] = []

    for (const entry of this.entries.values()) {
      if (allowed && !allowed.has(entry.name)) continue

      const name = entry.name.toLowerCase()
      const aliases = entry.aliases.map((a) => a.toLowerCase())
      const actions = entry.actions.map((a) => a.toLowerCase())
      const desc = entry.description.toLowerCase()
      const params = entry.params.toLowerCase()

      let score = 0
      const matched = new Set<string>()
      const fields = new Set<string>()

      // 整串精确命中：权重最高
      if (name === normalized) {
        score += 120
        fields.add("名称")
      }
      if (aliases.includes(normalized)) {
        score += 100
        fields.add("别名")
      }
      if (actions.includes(normalized)) {
        score += 95
        fields.add("操作")
      }

      for (const seg of segments) {
        if (name.includes(seg)) {
          score += FIELD_WEIGHT.name
          matched.add(seg)
          fields.add("名称")
        } else if (aliases.some((a) => a.includes(seg))) {
          score += FIELD_WEIGHT.alias
          matched.add(seg)
          fields.add("别名")
        } else if (actions.some((a) => a.includes(seg))) {
          score += FIELD_WEIGHT.action
          matched.add(seg)
          fields.add("操作")
        } else if (desc.includes(seg)) {
          score += FIELD_WEIGHT.description
          matched.add(seg)
          fields.add("描述")
        } else if (params.includes(seg)) {
          score += FIELD_WEIGHT.params
          matched.add(seg)
          fields.add("参数")
        }
      }

      // 覆盖度加成：命中片段越多越可能是用户想要的那个
      if (matched.size > 1) score += (matched.size - 1) * 8

      if (score > 0) {
        matches.push({ entry, score, matched: [...matched], fields: [...fields] })
      }
    }

    matches.sort((a, b) => b.score - a.score || a.entry.name.localeCompare(b.entry.name))
    return matches.slice(0, limit)
  }
}

/** 目录构建（异步，含插件工具） */
export async function buildToolCatalog(): Promise<ToolCatalog> {
  const tools = await getAllTools()
  const catalog = new ToolCatalog(tools)

  // 能力地图与真实工具的一致性自检：地图会注入 system prompt，漂移了就是模型被误导
  const issues = verifyCapabilityMap(catalog, AGENT_TOOL_CONFIG)
  if (issues.length) {
    console.warn("[CapabilityMap] 能力地图与工具目录不一致：")
    for (const issue of issues) console.warn(`  - ${issue}`)
  }

  return catalog
}

/**
 * 全局单例目录
 *
 * 插件加载是一次性的，没必要每个 Agent 节点都重建一份。
 */
let catalogPromise: Promise<ToolCatalog> | null = null

export function getToolCatalog(): Promise<ToolCatalog> {
  if (!catalogPromise) {
    catalogPromise = buildToolCatalog().catch((err) => {
      // 构建失败要能重试，不能把错误永久缓存住
      catalogPromise = null
      throw err
    })
  }
  return catalogPromise
}
