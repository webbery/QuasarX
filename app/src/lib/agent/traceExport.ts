/**
 * Agent 工作流痕迹导出（调试用）
 *
 * 两个用途：
 * - formatEventsMarkdown：把当前回合的事件流压成可读的 markdown（复制/贴进对话用）
 * - buildTraceJson：完整结构导出（当前回合事件 + 会话历史里的持久化思考），
 *   给「后面调试」用 —— JSON 保留全部字段与产物载荷，不丢信息
 *
 * 为什么消息用最小结构而不是直接 import chatStore：
 *   lib → stores 的反向依赖会成环（chatStore 自己 import lib/agent），
 *   这里只声明用到的字段，调用方（组件层）负责映射。
 */

import type { AgentEvent } from "./types"

/** 导出用消息最小结构 */
export interface TraceMessage {
  id: string
  role: string
  content: string
  timestamp: number
  thoughts?: Array<{ id: string; content: string; timestamp: number }>
  tokenUsage?: { promptTokens: number; completionTokens: number; totalTokens: number }
  error?: string
}

const EVENT_LABELS: Record<string, string> = {
  thought: "思考",
  tool_call: "调用工具",
  tool_result: "工具返回",
  artifact: "生成产物",
  response: "回复",
  error: "错误",
  gap: "能力缺口",
}

const AGENT_LABELS: Record<string, string> = {
  supervisor: "Supervisor",
  chat: "Chat",
  strategy: "Strategy",
  risk: "Risk",
  portfolio: "Portfolio",
  analysis: "Analysis",
}

function fmtTime(ts: number): string {
  const d = new Date(ts)
  const pad = (n: number) => String(n).padStart(2, "0")
  return `${pad(d.getHours())}:${pad(d.getMinutes())}:${pad(d.getSeconds())}`
}

/**
 * 当前回合的事件流 → 可读 markdown
 *
 * 每类事件一段，带时间戳 / Agent / 事件类型标签 / 内容。
 */
export function formatEventsMarkdown(events: AgentEvent[]): string {
  const header = [
    "# Agent 工作流记录",
    "",
    `导出时间：${new Date().toLocaleString("zh-CN")}`,
    `事件数：${events.length}`,
  ].join("\n")

  if (events.length === 0) return `${header}\n\n（本轮没有事件）`

  const body = events.map((e) => {
    const time = fmtTime(e.timestamp)
    const label = EVENT_LABELS[e.eventType] ?? e.eventType
    const agent = AGENT_LABELS[e.agent] ?? String(e.agent)
    return [
      `## [${time}] ${agent} · ${label}`,
      "",
      "```",
      e.content,
      "```",
    ].join("\n")
  })

  return `${header}\n\n${body.join("\n\n")}\n`
}

/**
 * 完整痕迹 → JSON 字符串（下载文件用）
 *
 * - currentRun：本轮完整事件流（含产物载荷，全保真）
 * - history：会话历史里的持久化思考与回复（thoughts 只持久化了 thought/gap 两类，
 *   tool_call/tool_result 只在 currentRun 里有）
 */
export function buildTraceJson(events: AgentEvent[], history: TraceMessage[]): string {
  return JSON.stringify(
    {
      exportedAt: new Date().toISOString(),
      eventCount: events.length,
      historyCount: history.length,
      currentRun: events,
      history: history.map((m) => ({
        id: m.id,
        role: m.role,
        content: m.content,
        timestamp: m.timestamp,
        thoughts: m.thoughts ?? [],
        tokenUsage: m.tokenUsage,
        error: m.error,
      })),
    },
    null,
    2,
  )
}

/** 生成下载文件名，如 agent-trace-20261122-143000.json */
export function traceFilename(ext = "json"): string {
  const d = new Date()
  const pad = (n: number) => String(n).padStart(2, "0")
  const stamp = `${d.getFullYear()}${pad(d.getMonth() + 1)}${pad(d.getDate())}-${pad(d.getHours())}${pad(d.getMinutes())}${pad(d.getSeconds())}`
  return `agent-trace-${stamp}.${ext}`
}
