/**
 * 记忆抽取：把一段对话抽取成 2–5 条叙事式事实
 *
 * - prompt 对齐论文 Appendix A.1（五维 what/when/where/who/why + 共指消解 + NEVER SUMMARIZE OR OMIT）
 * - 输出结构对齐 Appendix A.5.1 的 ExtractedFact
 * - 三级降级链：json_schema → json_object → 正则兜底
 * - 时间只认原文：normalizeOccurredRange 是 occurred_* 的唯一来源
 *
 * 纯函数（parseJsonPayload / normalizeExtractedFacts / normalizeOccurredRange / classifyError）
 * 与 IO（extractFacts）分开，前者可在 bench 里零 LLM 单测。
 */

import { z } from 'zod'

import type { FactType } from './types'

/** 抽取所需的聊天模型接口：能 invoke，可带超时与额外请求参数 */
export interface ChatInvoker {
  invoke(input: unknown, options?: unknown): Promise<{ content: unknown }>
  timeout?: number
  modelKwargs?: Record<string, unknown>
}

/* ------------------------------------------------------------------ */
/* 类型                                                                */
/* ------------------------------------------------------------------ */

/**
 * 失败分类
 *
 * - auth      凭证 / 配置问题（401 / 402 / 403 / 404）—— 重试无用，立即失败
 * - rateLimit 限流（429）
 * - server    服务端错误（5xx）
 * - timeout   超时
 * - network   网络层错误（DNS / 连接被拒 / 连接重置）
 * - parse     响应不是合法 JSON
 * - schema    结构不合格（含 400/422 —— 多半是 response_format 不被接受，需要降级）
 */
export type ErrorCategory =
  | 'auth'
  | 'rateLimit'
  | 'server'
  | 'timeout'
  | 'network'
  | 'parse'
  | 'schema'

/** 抽取器输入：一段对话 */
export interface ExtractionInput {
  /** 对话内容（user/assistant 混合） */
  transcript: string
  /** 发生时间参考（毫秒时间戳），用于「今天/昨天」这类相对表达 */
  now?: number
}

/** 一条抽取出的事实（对齐论文 A.5.1 的 ExtractedFact） */
export interface ExtractedFact {
  what: string
  when: string
  where: string
  who: string
  why: string
  factType: FactType
  /** 只由原文解析得到；原文没有日期时恒为 null */
  occurredStart: number | null
  /** 只由原文解析得到；原文没有日期时恒为 null */
  occurredEnd: number | null
  /** 记录时间（= 抽取时的 now），不采信 LLM */
  mentionedAt: number | null
  entities: string[]
}

/** 抽取结果（含降级标记与错误分类） */
export interface ExtractionResult {
  facts: ExtractedFact[]
  /** schema 不完整但补了默认值 → 这类样本不进「抽取质量」分母 */
  degraded: boolean
  /** 失败分类；成功时为 null */
  error: ErrorCategory | null
  /** 原始响应（调试用） */
  raw?: string
}

/* ------------------------------------------------------------------ */
/* 常量                                                                */
/* ------------------------------------------------------------------ */

/** 抽取请求的传输层超时（实测 deepseek 10–17s、qwen 48–85s，120s 留足余量） */
export const EXTRACTION_TIMEOUT_MS = 120_000

/** 最多尝试次数（首次 + 3 次退避重试） */
export const EXTRACTION_MAX_ATTEMPTS = 4

/** 退避间隔：1s / 4s / 16s，各带 ±25% 抖动 */
export const EXTRACTION_RETRY_DELAYS_MS: readonly number[] = [1_000, 4_000, 16_000]

/** 可重试的错误类别（auth 立即失败；其余都值得退避一次） */
const RETRYABLE: Record<ErrorCategory, boolean> = {
  auth: false,
  rateLimit: true,
  server: true,
  timeout: true,
  network: true,
  parse: true,
  schema: true,
}

/* ------------------------------------------------------------------ */
/* 抽取 prompt                                                         */
/* ------------------------------------------------------------------ */

/**
 * 抽取 prompt 模板
 *
 * 五条硬约束（每条都对应一次实测踩坑）：
 * 1. 强制中文输出 —— 不固定语言时 gold 片段命中率会从 3/3 掉到 1/3
 * 2. 日期不编造 —— 原文没有日期时模型会编出三个互不相同的日期，毒化时间衰减
 * 3. fact_type 归属 —— 我们的「我」是用户，用户的第一人称陈述属于 world
 * 4. assistant 的话不构成用户事实 —— 除非用户显式采纳
 * 5. 2–5 条覆盖整个 exchange —— 抽取模型天然碎片化，必须写死
 */
export const EXTRACTION_PROMPT_TEMPLATE = `你是记忆抽取器。把下面这段对话抽取成 2–5 条粗粒度的叙事事实，供日后检索使用。

## 输出格式（必须严格遵守）

只输出一个 JSON 对象，不要输出任何解释、前言、markdown 代码块标记或思考过程：

{"facts":[{"what":"...","when":"...","where":"...","who":"...","why":"...","fact_type":"world","occurred_start":null,"occurred_end":null,"mentioned_at":null,"entities":["实体1","实体2"]}]}

每条 fact 的字段：
- what：发生了什么 —— 完整描述，含全部具体信息（对象、动作、数量、参数、结论）
- when：什么时候 —— 原文给出的时间信息；没有就写「对话中未提及具体日期」
- where：在哪里 / 涉及什么范围（市场、账户、模块、地区等）；没有就写「不适用」
- who：谁参与 —— 用「用户」「assistant」或原文中的具体角色/机构，并写明关系
- why：为什么重要 / 动机 / 理由 / 偏好
- fact_type：world | experience | opinion | observation（见下方归属规则）
- occurred_start / occurred_end：ISO 日期字符串（如 "2026-03-20"）或 null，必须遵守「日期不编造」
- mentioned_at：null
- entities：实体名数组（中文，自由文本，没有就 []）

## LANGUAGE REQUIREMENT（强制中文）

- what / when / where / who / why 全部用中文书写，不要翻译成英文。
- 数字按中文习惯写：800 万、8%、1.5 倍、20%、3 个月、5000 元。
- entities 也用中文；专有名词（AkShare、LanceDB、qwen）保留原文写法。
- fact_type 的取值必须是英文枚举：world / experience / opinion / observation。

## 日期不编造（硬约束）

- 只有当对话原文里**显式出现**可解析的日期、月份或相对时间（如「2026年3月20日」「3月20日」「上周」「昨天」）时，才填 occurred_start / occurred_end。
- 否则 occurred_start 与 occurred_end 都必须为 null，并在 when 里写「对话中未提及具体日期」。
- 绝不要根据常识、训练数据或上下文猜测年份、月份或星期几。

## fact_type 归属规则（我们的语义，不是论文的）

- world：外部世界的客观事实 —— 用户陈述的参数、约束、决策、偏好、账户数据、市场数据。
  **用户在对话中的第一人称陈述（「我」「我的」）属于 world，不属于 experience。**
  例：用户说「把最大回撤收紧到 8%」→ world。
- experience：**对话中 assistant 自己**的经历 / 行为（「我建议…」「我查了…」「我执行了…」）。
  只有 assistant 的第一人称行为才是 experience。
- opinion：带主观判断的结论（对某个标的、策略、市场的评价）。
- observation：对某个实体的中立画像（跨多轮才能得出的事实性描述）。

## assistant 的话不是用户事实（硬约束）

- assistant 的建议、分析、结论，只有**用户显式采纳**（明确说「好」「就这么办」「按你说的」）时才构成用户的事实。
- 未经采纳的 assistant 建议只能作为 assistant 的 experience / opinion，不能写成用户的决定或参数。

## 粒度（硬约束）

- 输出 2–5 条事实。少于 2 条说明你漏了内容，多于 5 条说明你过度碎片化。
- 每条事实必须覆盖**整个 exchange**（一段完整的交流及其结论），不要把一个话题拆成多条孤立碎片。
- NEVER SUMMARIZE OR OMIT：保留原文中的具体数字、参数、专有名词、理由与结论；
  不要用「讨论了…」「提到了…」这类空泛概括代替细节。

## 选型与最终结论（硬约束）

- 涉及候选方案、标的或策略选型时，必须有且只有一条事实写最终结论，表述为「最终选定 X」。
- X 只写一个候选名称，不写别名、括号注释或额外限定词。
- X 使用原文最终决定句中的名称；候选列表与最终决定句的名称不一致时，以最终决定句为准。
- 被否决的候选可以合并成一条事实。

## 数字忠实（硬约束）

- what 中的每个数字都必须出现在原文中，不新增原文没有的计数。
- 原文没有的百分比、金额、数量、年份不写，不做单位换算。

## 共指消解（CRITICAL）

- 同一个人 / 物的不同说法必须合并成同一个指代，并补全关系。
  例：原文「我的室友 Emily」「她」→ 统一写成「Emily（用户的室友）」。
- 对话里一律把人类一方称「用户」，把模型一方称「assistant」。

## 输入

对话如下（user 与 assistant 混合）：

<dialogue>
{{TRANSCRIPT}}
</dialogue>`

/** 把对话正文填进 prompt 模板 */
export function buildExtractionPrompt(transcript: string): string {
  // 用函数形式做替换，避免正文里的 $& / $1 被当成替换模式
  return EXTRACTION_PROMPT_TEMPLATE.replace('{{TRANSCRIPT}}', () => transcript)
}

/* ------------------------------------------------------------------ */
/* 结构化输出 schema（论文 A.5.1）                                      */
/* ------------------------------------------------------------------ */

/**
 * response_format=json_schema 用的 schema
 *
 * 只当提示用：实测端点不强制（entities 会返回 string[]、可选字段时有时无），
 * 落地的校验仍然在 normalizeExtractedFacts 里。
 */
const FACTS_JSON_SCHEMA = {
  type: 'object',
  properties: {
    facts: {
      type: 'array',
      items: {
        type: 'object',
        properties: {
          what: { type: 'string' },
          when: { type: 'string' },
          where: { type: 'string' },
          who: { type: 'string' },
          why: { type: 'string' },
          fact_type: {
            type: 'string',
            enum: ['world', 'experience', 'opinion', 'observation'],
          },
          occurred_start: { type: ['string', 'null'] },
          occurred_end: { type: ['string', 'null'] },
          mentioned_at: { type: ['string', 'null'] },
          entities: {
            type: 'array',
            items: {
              type: 'object',
              properties: { text: { type: 'string' } },
              required: ['text'],
              additionalProperties: false,
            },
          },
        },
        required: ['what', 'when', 'where', 'who', 'why', 'fact_type'],
        additionalProperties: false,
      },
    },
  },
  required: ['facts'],
  additionalProperties: false,
} as const

/* ------------------------------------------------------------------ */
/* 时间归一化（C2）                                                     */
/* ------------------------------------------------------------------ */

/**
 * 从原文抽时间表达式 → 绝对时间戳区间；抽不到返回 null
 *
 * 规则优先，按模式优先级取第一个命中（绝对日期优先于相对表达）。
 * 区间一律按本地时区的自然日 / 自然月 / 自然年计算。
 *
 * 支持：
 * - 绝对：2026年3月20日 / 2026年3月20号 / 2026年3月 / 2026年
 * - 半绝对：3月20日（年份按 now 推断：晚于今天 → 去年）/ 3月
 * - 相对日：今天 / 今日 / 昨天 / 昨日 / 前天 / 明天 / 后天 / N天前
 * - 相对周（周一为一周起点）：本周 / 这周 / 上周 / 下周 / 上周三 / N周前
 * - 相对月：本月 / 这个月 / 上个月 / 上月 / 下个月 / N个月前
 * - 相对年：今年 / 去年 / 明年
 *
 * 宁可没有时间，也不要错时间 —— 错时间会毒化时间衰减排序。
 */
export function normalizeOccurredRange(
  text: string,
  now: number,
): { start: number; end: number } | null {
  if (typeof text !== 'string' || !text) return null
  if (typeof now !== 'number' || !Number.isFinite(now)) return null

  const base = new Date(now)
  const year = base.getFullYear()
  const month = base.getMonth()

  // 1. 「2026年3月20日」/「2026年3月20号」
  let m = text.match(/(\d{4})\s*年\s*(\d{1,2})\s*月\s*(\d{1,2})\s*[日号]/)
  if (m) {
    const y = Number(m[1])
    const mo = Number(m[2]) - 1
    const d = Number(m[3])
    return isValidYmd(y, mo, d) ? dayRange(y, mo, d) : null
  }

  // 2. 「2026年3月」
  m = text.match(/(\d{4})\s*年\s*(\d{1,2})\s*月/)
  if (m) {
    const y = Number(m[1])
    const mo = Number(m[2]) - 1
    return isValidYm(y, mo) ? monthRange(y, mo) : null
  }

  // 3. 「2026年」
  m = text.match(/(\d{4})\s*年/)
  if (m) {
    const y = Number(m[1])
    return y >= 1970 && y <= 2100 ? yearRange(y) : null
  }

  // 4. 「3月20日」（没写年份：今年还没到就按去年算）
  m = text.match(/(\d{1,2})\s*月\s*(\d{1,2})\s*[日号]/)
  if (m) {
    const mo = Number(m[1]) - 1
    const d = Number(m[2])
    if (!isValidYmd(year, mo, d)) return null
    const thisYear = dayRange(year, mo, d)
    return thisYear.start > endOfLocalDay(base) ? dayRange(year - 1, mo, d) : thisYear
  }

  // 5. 「3月」（没写年份：已经过去了就按今年，否则按去年）
  m = text.match(/(\d{1,2})\s*月/)
  if (m) {
    const mo = Number(m[1]) - 1
    if (mo < 0 || mo > 11) return null
    const thisYear = monthRange(year, mo)
    return thisYear.start > now ? monthRange(year - 1, mo) : thisYear
  }

  // 6. 相对日
  if (/昨天|昨日/.test(text)) return dayRangeFromBase(base, -1)
  if (/前天/.test(text)) return dayRangeFromBase(base, -2)
  if (/今天|今日/.test(text)) return dayRangeFromBase(base, 0)
  if (/明天|明日/.test(text)) return dayRangeFromBase(base, 1)
  if (/后天/.test(text)) return dayRangeFromBase(base, 2)

  // 7. 相对周（周一为一周起点）
  m = text.match(/上周\s*([一二三四五六日天])/)
  if (m) {
    const weekday = CN_WEEKDAY_INDEX[m[1]]
    if (weekday != null) return dayAtWeekOffset(base, -1, weekday)
  }
  if (/上周|上星期|上礼拜/.test(text)) return weekRange(base, -1)
  if (/本周|这周|这个星期|本星期|这礼拜/.test(text)) return weekRange(base, 0)
  if (/下周|下星期|下礼拜/.test(text)) return weekRange(base, 1)

  // 8. 相对月
  if (/上个月|上月/.test(text)) return monthRange(year, month - 1)
  if (/本月|这个月|当月/.test(text)) return monthRange(year, month)
  if (/下个月|下月/.test(text)) return monthRange(year, month + 1)

  // 9. 相对年
  if (/今年|本年/.test(text)) return yearRange(year)
  if (/去年/.test(text)) return yearRange(year - 1)
  if (/明年/.test(text)) return yearRange(year + 1)

  // 10. N 天前 / N 周前 / N 个月前
  m = text.match(/(\d+)\s*天\s*(?:以)?前/)
  if (m) {
    const n = Number(m[1])
    return n >= 1 && n <= 3650 ? dayRangeFromBase(base, -n) : null
  }
  m = text.match(/(\d+)\s*(?:周|个星期|星期)\s*(?:以)?前/)
  if (m) {
    const n = Number(m[1])
    return n >= 1 && n <= 520 ? dayRangeFromBase(base, -7 * n) : null
  }
  m = text.match(/(\d+)\s*个月\s*(?:以)?前/)
  if (m) {
    const n = Number(m[1])
    return n >= 1 && n <= 120 ? monthRange(year, month - n) : null
  }

  return null
}

/** 中文星期字 → 距周一的偏移（周一 = 0 … 周日 = 6） */
const CN_WEEKDAY_INDEX: Record<string, number> = {
  一: 0,
  二: 1,
  三: 2,
  四: 3,
  五: 4,
  六: 5,
  日: 6,
  天: 6,
}

/** 本地时区自然日区间 */
function dayRange(y: number, m: number, d: number): { start: number; end: number } {
  return {
    start: new Date(y, m, d, 0, 0, 0, 0).getTime(),
    end: new Date(y, m, d, 23, 59, 59, 999).getTime(),
  }
}

/** 本地时区自然月区间（m 可为负数 / 超过 11，Date 会归一化） */
function monthRange(y: number, m: number): { start: number; end: number } {
  return {
    start: new Date(y, m, 1, 0, 0, 0, 0).getTime(),
    end: new Date(y, m + 1, 0, 23, 59, 59, 999).getTime(),
  }
}

/** 本地时区自然年区间 */
function yearRange(y: number): { start: number; end: number } {
  return {
    start: new Date(y, 0, 1, 0, 0, 0, 0).getTime(),
    end: new Date(y, 11, 31, 23, 59, 59, 999).getTime(),
  }
}

/** 以 base 所在日为锚，偏移 n 天 */
function dayRangeFromBase(base: Date, offsetDays: number): { start: number; end: number } {
  return dayRange(base.getFullYear(), base.getMonth(), base.getDate() + offsetDays)
}

/** 当天 23:59:59.999 */
function endOfLocalDay(base: Date): number {
  return new Date(base.getFullYear(), base.getMonth(), base.getDate(), 23, 59, 59, 999).getTime()
}

/** base 所在周的周一 00:00（本地时区） */
function startOfLocalWeek(base: Date): Date {
  const day = new Date(base.getFullYear(), base.getMonth(), base.getDate())
  const offset = (day.getDay() + 6) % 7 // 周日 = 0 → 偏移 6
  day.setDate(day.getDate() - offset)
  return day
}

/** 相对周区间：weekOffset = -1 上周 / 0 本周 / 1 下周（周一 00:00 → 周日 23:59:59.999） */
function weekRange(base: Date, weekOffset: number): { start: number; end: number } {
  const monday = startOfLocalWeek(base)
  monday.setDate(monday.getDate() + weekOffset * 7)
  return {
    start: monday.getTime(),
    end: new Date(monday.getFullYear(), monday.getMonth(), monday.getDate() + 6, 23, 59, 59, 999).getTime(),
  }
}

/** 相对周里的某一天（weekdayIndex：周一 = 0 … 周日 = 6） */
function dayAtWeekOffset(
  base: Date,
  weekOffset: number,
  weekdayIndex: number,
): { start: number; end: number } {
  const monday = startOfLocalWeek(base)
  monday.setDate(monday.getDate() + weekOffset * 7 + weekdayIndex)
  return dayRange(monday.getFullYear(), monday.getMonth(), monday.getDate())
}

/** 合法公历日 */
function isValidYmd(y: number, m: number, d: number): boolean {
  if (!Number.isInteger(y) || y < 1970 || y > 2100) return false
  if (!Number.isInteger(m) || m < 0 || m > 11) return false
  if (!Number.isInteger(d) || d < 1 || d > 31) return false
  const dt = new Date(y, m, d)
  return dt.getFullYear() === y && dt.getMonth() === m && dt.getDate() === d
}

/** 合法年月 */
function isValidYm(y: number, m: number): boolean {
  return Number.isInteger(y) && y >= 1970 && y <= 2100 && Number.isInteger(m) && m >= 0 && m <= 11
}

/* ------------------------------------------------------------------ */
/* JSON 提取（三级降级链的第三级）                                      */
/* ------------------------------------------------------------------ */

/**
 * 从回复正文里抠 JSON 文本
 *
 * 两条路径照抄 IndexAgent.ts:48-66 的写法：
 * 1. 直接匹配最外层对象
 * 2. markdown 代码块里的对象
 * 另外补一条数组兜底（模型偶尔只返回顶层数组）。
 */
function extractJsonText(content: string): string | null {
  if (typeof content !== 'string' || !content) return null

  const direct = content.match(/\{[\s\S]*\}/)
  if (direct) return direct[0]

  const codeBlock = content.match(/```(?:json)?\s*([\s\S]*?)```/)
  if (codeBlock) {
    const inner = codeBlock[1].trim()
    const matched = inner.match(/\{[\s\S]*\}/)
    if (matched) return matched[0]
    if (inner) return inner
  }

  const array = content.match(/\[[\s\S]*\]/)
  if (array) return array[0]

  // 截断兜底：从第一个 [ / { 切到末尾，交给 repairTruncatedJson 补括号
  const firstObject = content.indexOf('{')
  const firstArray = content.indexOf('[')
  if (firstArray >= 0 && (firstObject < 0 || firstArray < firstObject)) {
    return content.slice(firstArray)
  }
  if (firstObject >= 0) return content.slice(firstObject)

  return null
}

/**
 * 解析 LLM 回复正文 → JSON 值
 *
 * 解析失败返回 null（调用方归类为 parse 并降级重试）。
 * 末尾被截断时尝试补齐括号后再解析一次。
 */
export function parseJsonPayload(raw: string): unknown | null {
  const text = extractJsonText(raw)
  if (!text) return null

  try {
    return JSON.parse(text) as unknown
  } catch {
    const repaired = repairTruncatedJson(text)
    if (!repaired) return null
    try {
      return JSON.parse(repaired) as unknown
    } catch {
      return null
    }
  }
}

/** 补齐被截断的 JSON 的闭合括号；没有未闭合的括号则返回 null */
function repairTruncatedJson(text: string): string | null {
  const stack: string[] = []
  let inString = false
  let escaped = false

  for (const ch of text) {
    if (inString) {
      if (escaped) escaped = false
      else if (ch === '\\') escaped = true
      else if (ch === '"') inString = false
      continue
    }
    if (ch === '"') inString = true
    else if (ch === '{' || ch === '[') stack.push(ch)
    else if (ch === '}' || ch === ']') stack.pop()
  }

  if (!stack.length && !inString) return null

  let out = text
  if (inString) out += '"'
  while (stack.length) {
    out += stack.pop() === '{' ? '}' : ']'
  }
  return out
}

/* ------------------------------------------------------------------ */
/* 校验 + 归一化                                                       */
/* ------------------------------------------------------------------ */

/** 单条事实的宽松 schema：只要求是对象，字段类型交给 readDimension 兜底 */
const rawFactSchema = z.record(z.string(), z.unknown())

/** JSON.parse 之后的一条原始事实 */
type RawFact = Record<string, unknown>

/** 支持的事实类型（论文 3 值 + observation，共 4 值） */
const FACT_TYPE_ALIASES: Record<string, FactType> = {
  world: 'world',
  experience: 'experience',
  opinion: 'opinion',
  observation: 'observation',
  世界: 'world',
  事实: 'world',
  经历: 'experience',
  体验: 'experience',
  观点: 'opinion',
  意见: 'opinion',
  观察: 'observation',
}

/**
 * 校验并归一化 LLM 返回的 JSON（纯函数）
 *
 * - 五维缺失 → 补 ''，计 degraded
 * - fact_type 缺失 / 越界 → 'world'，计 degraded
 * - entities 缺失 → []，计 degraded；`string[]` 与 `[{text}]` 两种形状都接受
 * - occurred_* 只从**原文**解析；原文没有日期时一律 null（LLM 编的日期直接丢弃）
 *
 * @returns null 表示外壳结构不可用（schema 错误）；否则返回补过默认值的事实
 */
export function normalizeExtractedFacts(
  payload: unknown,
  transcript: string,
  now: number,
): { facts: ExtractedFact[]; degraded: boolean } | null {
  const list = readFactList(payload)
  if (!list) return null

  // 原文只有一处时间落点 → 本批事实共用；原文没有 → 全部 null
  const range = normalizeOccurredRange(typeof transcript === 'string' ? transcript : '', now)
  const mentionedAt = typeof now === 'number' && Number.isFinite(now) ? now : null

  let degraded = list.degraded
  const facts: ExtractedFact[] = []

  for (const item of list.items) {
    const what = readDimension(item, 'what')
    const when = readDimension(item, 'when')
    const where = readDimension(item, 'where')
    const who = readDimension(item, 'who')
    const why = readDimension(item, 'why')
    const factType = readFactType(item.fact_type)
    const entities = readEntities(item.entities)

    if (
      what.degraded ||
      when.degraded ||
      where.degraded ||
      who.degraded ||
      why.degraded ||
      factType.degraded ||
      entities.degraded
    ) {
      degraded = true
    }

    facts.push({
      what: what.value,
      when: when.value,
      where: where.value,
      who: who.value,
      why: why.value,
      factType: factType.value,
      occurredStart: range ? range.start : null,
      occurredEnd: range ? range.end : null,
      mentionedAt,
      entities: entities.value,
    })
  }

  return { facts, degraded }
}

/** 取出事实数组：兼容裸数组 / {facts} / {items} / {data} / {results} */
function readFactList(payload: unknown): { items: RawFact[]; degraded: boolean } | null {
  if (Array.isArray(payload)) {
    const parsed = z.array(rawFactSchema).safeParse(payload)
    return parsed.success ? { items: parsed.data, degraded: false } : null
  }
  if (!payload || typeof payload !== 'object') return null

  const envelope = z.object({}).catchall(z.unknown()).safeParse(payload)
  if (!envelope.success) return null

  for (const key of ['facts', 'items', 'data', 'results']) {
    const value = envelope.data[key]
    if (!Array.isArray(value)) continue
    const parsed = z.array(rawFactSchema).safeParse(value)
    if (!parsed.success) return null
    // 外壳不是标准 facts 键 → 也算形状上的降级
    return { items: parsed.data, degraded: key !== 'facts' }
  }
  return null
}

/** 读一个必填维度：缺 / 空串 → '' 且 degraded；数字/布尔保留信息但算降级 */
function readDimension(item: RawFact, key: string): { value: string; degraded: boolean } {
  const value = item[key]
  if (typeof value === 'string') {
    const trimmed = value.trim()
    return { value: trimmed, degraded: trimmed.length === 0 }
  }
  if (typeof value === 'number' || typeof value === 'boolean') {
    return { value: String(value), degraded: true }
  }
  return { value: '', degraded: true }
}

/** 读 fact_type：缺失 / 越界一律映射到 world 并计降级 */
function readFactType(value: unknown): { value: FactType; degraded: boolean } {
  if (typeof value === 'string') {
    const key = value.trim().toLowerCase().replace(/[\s_-]+/g, '')
    const mapped = FACT_TYPE_ALIASES[key]
    if (mapped) return { value: mapped, degraded: false }
  }
  return { value: 'world', degraded: true }
}

/** 读 entities：接受 string[] 与论文的 [{text}]；缺失 → [] 并计降级 */
function readEntities(value: unknown): { value: string[]; degraded: boolean } {
  if (value == null) return { value: [], degraded: true }

  if (typeof value === 'string') {
    const parts = value
      .split(/[,，、;；]/)
      .map((s) => s.trim())
      .filter(Boolean)
    return { value: dedupe(parts), degraded: true }
  }

  if (!Array.isArray(value)) return { value: [], degraded: true }

  const out: string[] = []
  let degraded = false
  for (const entry of value) {
    if (typeof entry === 'string') {
      const text = entry.trim()
      if (text) out.push(text)
      continue
    }
    if (entry && typeof entry === 'object' && !Array.isArray(entry)) {
      const raw = entry as { text?: unknown; name?: unknown }
      const text = typeof raw.text === 'string' ? raw.text : raw.name
      if (typeof text === 'string' && text.trim()) out.push(text.trim())
      else degraded = true
      continue
    }
    degraded = true
  }
  return { value: dedupe(out), degraded }
}

/** 去重（保留首次出现的顺序） */
function dedupe(values: string[]): string[] {
  return [...new Set(values)]
}

/* ------------------------------------------------------------------ */
/* 错误分类                                                            */
/* ------------------------------------------------------------------ */

/**
 * 把任意异常映射到 ErrorCategory
 *
 * 状态码优先（OpenAI SDK 的 APIError 带 status）；
 * 400/409/422 归 schema —— 多半是 response_format 不被端点接受，应该降级而不是放弃。
 */
export function classifyError(e: unknown): ErrorCategory {
  if (e instanceof z.ZodError) return 'schema'

  const status = pickStatus(e)
  if (status != null) {
    if (status === 401 || status === 402 || status === 403 || status === 404) return 'auth'
    if (status === 408) return 'timeout'
    if (status === 429) return 'rateLimit'
    if (status >= 500 && status <= 599) return 'server'
    if (status === 400 || status === 409 || status === 422) return 'schema'
  }

  const name = e instanceof Error ? e.name : ''
  const message = e instanceof Error ? e.message : String(e ?? '')
  const text = `${name} ${message}`.toLowerCase()

  if (name === 'AbortError' || name === 'TimeoutError' || /timeout|timed out|etimedout/.test(text)) {
    return 'timeout'
  }
  if (/rate.?limit|too many requests/.test(text)) return 'rateLimit'
  if (
    /unauthorized|forbidden|invalid api key|incorrect api key|authentication|payment required/.test(
      text,
    )
  ) {
    return 'auth'
  }
  if (/internal server error|bad gateway|service unavailable|gateway timeout|overloaded/.test(text)) {
    return 'server'
  }
  if (e instanceof SyntaxError || /unexpected token|unexpected end of json|json parse|json\.parse/.test(text)) {
    return 'parse'
  }
  if (/econnrefused|econnreset|enotfound|eai_again|epipe|fetch failed|socket hang up|network|dns/.test(text)) {
    return 'network'
  }

  // 分类不了的当网络瞬时故障：值得退避一次，但不该无限重试
  return 'network'
}

/** 从异常的常见挂载点取 HTTP 状态码 */
function pickStatus(e: unknown): number | null {
  const seen = new Set<unknown>()
  let current: unknown = e

  for (let depth = 0; depth < 4; depth++) {
    if (!current || typeof current !== 'object' || seen.has(current)) break
    const obj = current as Record<string, unknown>
    seen.add(current)

    for (const key of ['status', 'statusCode', 'code']) {
      const value = obj[key]
      if (typeof value === 'number' && value >= 100 && value <= 599) return value
    }

    const response = obj.response
    if (response && typeof response === 'object') {
      const value = (response as Record<string, unknown>).status
      if (typeof value === 'number' && value >= 100 && value <= 599) return value
    }

    current = obj.cause
  }

  return null
}

/* ------------------------------------------------------------------ */
/* 抽取入口                                                            */
/* ------------------------------------------------------------------ */

/** 三级降级档 */
type ExtractionTier = 'json_schema' | 'json_object' | 'plain'

/** 降级顺序：结构化最强 → 弱 → 正则兜底 */
const EXTRACTION_TIERS: readonly ExtractionTier[] = ['json_schema', 'json_object', 'plain']

/**
 * 抽取一段对话里的事实
 *
 * @param input 对话正文 + 时间参考（now 缺省用当前时间）
 * @param llm   已构造好的 LLM 实例（调用方传 `createLLM(cfg, 0)`；
 *              这里不再套 LangChain 的默认重试，否则 429 最坏退避 34 分钟）
 *
 * 重试策略：
 * - auth（401/402/403/404）→ 立即返回，不重试
 * - parse / schema → 重试并推进降级档（json_schema → json_object → 正则）
 * - rateLimit / server / timeout / network → 退避重试（1s / 4s / 16s ± 25% 抖动）
 * - 空对话 → 0 条、error=null（不是失败）
 */
export async function extractFacts(
  input: ExtractionInput,
  llm: ChatInvoker,
): Promise<ExtractionResult> {
  const transcript = typeof input?.transcript === 'string' ? input.transcript.trim() : ''
  if (!transcript) return { facts: [], degraded: false, error: null }

  const now =
    typeof input.now === 'number' && Number.isFinite(input.now) ? input.now : Date.now()
  const prompt = buildExtractionPrompt(transcript)

  let tierIndex = 0
  let lastError: ErrorCategory | null = null
  let lastRaw: string | undefined

  for (let attempt = 0; attempt < EXTRACTION_MAX_ATTEMPTS; attempt++) {
    if (attempt > 0) {
      await sleep(backoffDelay(attempt - 1))
    }

    const tier = EXTRACTION_TIERS[Math.min(tierIndex, EXTRACTION_TIERS.length - 1)]

    try {
      const raw = await invokeOnce(llm, tier, prompt)
      lastRaw = raw

      const payload = parseJsonPayload(raw)
      if (payload == null) {
        lastError = 'parse'
        tierIndex = Math.min(tierIndex + 1, EXTRACTION_TIERS.length - 1)
        continue
      }

      const normalized = normalizeExtractedFacts(payload, transcript, now)
      if (!normalized) {
        lastError = 'schema'
        tierIndex = Math.min(tierIndex + 1, EXTRACTION_TIERS.length - 1)
        continue
      }

      return { facts: normalized.facts, degraded: normalized.degraded, error: null, raw }
    } catch (e) {
      lastError = classifyError(e)
      if (!RETRYABLE[lastError]) {
        return { facts: [], degraded: false, error: lastError, raw: lastRaw }
      }
      if (lastError === 'parse' || lastError === 'schema') {
        tierIndex = Math.min(tierIndex + 1, EXTRACTION_TIERS.length - 1)
      }
    }
  }

  return { facts: [], degraded: false, error: lastError ?? 'network', raw: lastRaw }
}

/**
 * 发一次请求
 *
 * - 传输层超时设为 120s（不覆盖调用方更严格的超时）
 * - 额外用 AbortController 兜底：SDK 的 timeout 不生效时也能中断
 * - response_format 通过 modelKwargs 透传（LangChain 的透传通道）
 */
async function invokeOnce(llm: ChatInvoker, tier: ExtractionTier, prompt: string): Promise<string> {

  if (
    typeof llm.timeout !== 'number' ||
    llm.timeout <= 0 ||
    llm.timeout > EXTRACTION_TIMEOUT_MS
  ) {
    llm.timeout = EXTRACTION_TIMEOUT_MS
  }

  const kwargs = { ...(llm.modelKwargs ?? {}) }
  const format = responseFormatFor(tier)
  if (format) kwargs.response_format = format
  else delete kwargs.response_format
  llm.modelKwargs = kwargs

  const controller = new AbortController()
  const timer = setTimeout(() => controller.abort(), EXTRACTION_TIMEOUT_MS + 5_000)

  try {
    const response = await llm.invoke([{ role: 'user', content: prompt }] as any, {
      signal: controller.signal,
    })
    return contentToText(response.content)
  } finally {
    clearTimeout(timer)
  }
}

/** 各降级档的 response_format */
function responseFormatFor(tier: ExtractionTier): unknown {
  if (tier === 'json_schema') {
    return {
      type: 'json_schema',
      json_schema: {
        name: 'extracted_facts',
        // 端点不强制 schema，strict 只会让不支持的端点直接 400，所以关掉
        strict: false,
        schema: FACTS_JSON_SCHEMA,
      },
    }
  }
  if (tier === 'json_object') return { type: 'json_object' }
  return undefined
}

/** 消息 content 可能是字符串或内容块数组 */
function contentToText(content: unknown): string {
  if (typeof content === 'string') return content
  if (Array.isArray(content)) {
    return content
      .map((part) => {
        if (typeof part === 'string') return part
        const text = (part as { text?: unknown } | null)?.text
        return typeof text === 'string' ? text : ''
      })
      .join('')
  }
  return ''
}

/** 第 index 次重试的等待时长（1s / 4s / 16s，±25% 抖动） */
function backoffDelay(index: number): number {
  const base =
    EXTRACTION_RETRY_DELAYS_MS[Math.min(index, EXTRACTION_RETRY_DELAYS_MS.length - 1)] ?? 16_000
  const factor = 0.75 + Math.random() * 0.5
  return Math.round(base * factor)
}

/** sleep */
function sleep(ms: number): Promise<void> {
  return new Promise((resolve) => setTimeout(resolve, ms))
}
