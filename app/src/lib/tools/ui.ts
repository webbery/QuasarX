/**
 * UI 工具（常驻）
 *
 * Agent 只决定「要不要展示界面」，具体画什么交给 builder：
 *   ui(action="builders")                       → 可用构建器清单（id + 一句话 + 输入形状 + 手势）
 *   ui(action="build", builder="timeseries")    → 校验 input → 产物 → 前端渲染
 *
 * 为什么常驻而不是走 search → enable：
 *   「要不要展示界面」是每一轮都可能出现的判断，不该多一次检索。
 *   常驻 schema 只有 action/builder/input 三个字段，builder 的输入形状按需取。
 *
 * 两类 builder 的 input 语义不同（builders 的输出里会写明）：
 *   - 你给数据（timeseries / histogram / matrix / table / metric）：input 就是数据
 *   - 内部流式（progress）：hidden，只能由工具/动作产生，模型不能构建
 */

import { tool } from '@langchain/core/tools'
import { z } from 'zod'
import { getUiType, listBuildableUiTypes, mergeParams, registerUiType } from '@/lib/charts/registry'
import { getResolver } from '@/lib/charts/resolvers'
import { UI_TYPE_DEFS } from '@/lib/charts/uiTypes'
import { UI_SPEC_VERSION, type UiSpec } from '@/lib/ui/protocol'
import { makeArtifact, type ToolArtifactResult } from '@/lib/agent/artifacts'

// 先登记契约，builder 列表与校验都从注册表来
for (const def of UI_TYPE_DEFS) {
  registerUiType(def)
}

/* ============================================================================
 * 时刻标注
 * ========================================================================== */

const momentSchema = z.object({
  date: z.string().describe('时刻，YYYY-MM-DD'),
  title: z.string().describe('该时刻的标题，如「变点检测触发」'),
  detail: z.string().optional().describe('详细说明'),
  severity: z.enum(['info', 'positive', 'warning', 'critical']).optional(),
})

type MomentInput = z.infer<typeof momentSchema>

/** 把 YYYY-MM-DD 解析成当日 15:00 的 Unix 秒 */
function parseMomentTimestamp(date: string): number | null {
  const m = date.match(/^(\d{4})-(\d{2})-(\d{2})/)
  if (!m) return null
  const ts = new Date(Number(m[1]), Number(m[2]) - 1, Number(m[3]), 15, 0, 0).getTime()
  return Number.isNaN(ts) ? null : Math.floor(ts / 1000)
}

/** 在时间序列中就近匹配某个时刻的下标 */
function nearestIndex(timestamps: number[], ts: number): number {
  if (!timestamps.length) return 0
  let best = 0
  let bestDiff = Infinity
  for (let i = 0; i < timestamps.length; i++) {
    const diff = Math.abs(timestamps[i] - ts)
    if (diff < bestDiff) {
      bestDiff = diff
      best = i
    }
  }
  return best
}

/** 把 moments 标注写进产物数据（只对带 x 时间轴的数据生效） */
function applyMoments(data: Record<string, unknown>, moments?: MomentInput[]): void {
  if (!moments?.length) return
  const x = data.x
  if (!Array.isArray(x) || x.length === 0) return

  const markers: Array<{ x: number; kind: string; label: string }> = []
  const momentList: Array<Record<string, unknown>> = []

  moments.forEach((input, i) => {
    const ts = parseMomentTimestamp(input.date)
    if (ts === null) return
    const idx = nearestIndex(x as number[], ts)
    markers.push({ x: idx, kind: 'point', label: input.title })
    momentList.push({
      id: `moment-${i}`,
      timestamp: (x as number[])[idx],
      title: input.title,
      detail: input.detail,
      severity: input.severity,
    })
  })

  if (momentList.length) {
    data.markers = markers
    data.moments = momentList
  }
}

/* ============================================================================
 * builder 清单
 * ========================================================================== */

/** 数据来源：显式声明优先，没写就按有没有 resolver 推断 */
function intakeOf(def: { intake?: string; resolver?: string }): 'llm' | 'resolver' | 'stream' {
  if (def.intake === 'resolver' || def.intake === 'stream') return def.intake
  return def.resolver ? 'resolver' : 'llm'
}

/** 生成 builder 清单文本（给 LLM 看，只列不 hidden 的） */
function buildersText(): string {
  const defs = listBuildableUiTypes()
  const lines = [
    `【UI 构建器】共 ${defs.length} 个。用 ui(action="build", builder="<id>", input={...}) 构建。`,
    `同一组数据只选一种最合适的表示，不要为同一内容重复建多张卡（如直方图与频次表二选一）。`,
  ]

  for (const def of defs) {
    const intake =
      intakeOf(def) === 'resolver'
        ? '系统取数（input 传查询参数，数据由系统取）'
        : '你给数据（input 就是数据本身）'
    const gestures = def.gestures?.length ? ` · 手势：${def.gestures.join('/')}` : ''
    lines.push('', `- **${def.id}**（${def.label}）· ${intake}${gestures}`)

    if (intakeOf(def) === 'resolver') {
      const keys = def.controls.map((c) => `${c.key}=${c.label}`).join(', ')
      lines.push(`  参数：${keys || '无'}`)
    } else {
      lines.push(`  数据形状：${def.schemaDescription}`)
    }
  }

  return lines.join('\n')
}

/* ============================================================================
 * 工具
 * ========================================================================== */

export const uiTool = tool(
  async (params) => {
    const { action, builder, title, subtitle, source, input } = params

    /* ---------- 列构建器 ---------- */
    if (action === 'builders') {
      return buildersText()
    }

    /* ---------- 构建 ---------- */
    if (!builder) {
      return `错误：build 需要 builder 参数。可用：${listBuildableUiTypes().map((d) => d.id).join(' / ')}`
    }

    const def = getUiType(builder)
    if (!def) {
      return `未知构建器「${builder}」。可用：${listBuildableUiTypes().map((d) => d.id).join(' / ')}`
    }
    if (def.hidden) {
      return `「${builder}」是内部类型，由工具自动产生，不能直接构建。`
    }

    const payload = (input ?? {}) as Record<string, unknown>
    const specTitle = title?.trim() || def.label
    const baseSpec = {
      version: UI_SPEC_VERSION,
      type: def.id,
      title: specTitle,
      subtitle,
      source,
    }

    /* ---------- 系统取数 ---------- */
    if (def.resolver) {
      const resolver = getResolver(def.resolver)
      if (!resolver) {
        return `错误：构建器「${def.id}」声明了 resolver「${def.resolver}」，但它没有注册。`
      }

      const { moments, ...query } = payload as Record<string, unknown> & { moments?: MomentInput[] }
      const mergedParams = mergeParams(def, query)

      try {
        const out = await resolver(mergedParams)
        const data = { ...(out.data as Record<string, unknown>) }
        applyMoments(data, moments)

        const spec: UiSpec = {
          ...baseSpec,
          params: mergedParams,
          data,
          controls: out.controls ?? def.controls,
        }
        const artifact = makeArtifact('ui', specTitle, spec, {
          subtitle,
          source: source ?? `ui · ${def.id}`,
        })
        return {
          text: `已生成「${specTitle}」（${def.label}），数据由系统取。用户在产物卡上可切换参数。这些数值已展示给用户，正文不要再重复列出，只做结论与解读。`,
          artifact,
        } as ToolArtifactResult
      } catch (e) {
        const message = e instanceof Error ? e.message : String(e)
        return `生成「${specTitle}」失败：${message}`
      }
    }

    /* ---------- 你给数据 ---------- */
    const parsed = def.schema.safeParse(payload)
    if (!parsed.success) {
      const issues = parsed.error.issues
        .slice(0, 6)
        .map((i) => `  - ${i.path.join('.') || '(根)'}：${i.message}`)
        .join('\n')
      return [
        `input 不符合「${def.id}」的要求，未生成界面。`,
        ``,
        `期望的 input 形状：${def.schemaDescription}`,
        ``,
        `具体问题：`,
        issues,
      ].join('\n')
    }

    const data = { ...(parsed.data as Record<string, unknown>) }
    const spec: UiSpec = {
      ...baseSpec,
      params: mergeParams(def, undefined),
      data,
      controls: def.controls,
    }
    const artifact = makeArtifact('ui', specTitle, spec, {
      subtitle,
      source: source ?? `ui · ${def.id}`,
    })

    return {
      text: `已生成「${specTitle}」（${def.label}）。${summarize(def.id, data)}这些数值已展示给用户，正文不要再重复列出，只做结论与解读。`,
      artifact,
    } as ToolArtifactResult
  },
  {
    name: 'ui',
    description:
      '界面构建工具（常驻）。结果适合可视化、需要用户输入、或长任务需要进度时产出界面；' +
      '纯文字回答更合适时不要调用。\n' +
      "1) action='builders' —— 查可用构建器（id、输入形状、可用手势）。第一次用某类型前先查。\n" +
      "2) action='build' builder='<id>' title='<标题>' input={...} —— 构建并渲染：\n" +
      '   系统取数型：input 传查询参数，数据由系统取；其余类型：input 就是数据本身。\n' +
      '数值必须来自 Tool 返回或用户提供，禁止编造；时间戳用 Unix 秒，百分比用小数。',
    schema: z.object({
      action: z.enum(['builders', 'build']).describe('builders=列出可用构建器；build=构建界面'),
      builder: z.string().optional().describe('构建器 id（build 时必填），取值见 builders 的输出'),
      title: z.string().optional().describe('界面标题（build 时建议给）'),
      subtitle: z.string().optional().describe('副标题'),
      source: z.string().optional().describe('数据来源说明'),
      input: z
        .record(z.string(), z.any())
        .optional()
        .describe('构建器输入：系统取数型传查询参数，其余类型传数据本身'),
    }),
  },
)

/** 生成一句话的数据摘要（回灌给 LLM） */
function summarize(type: string, data: Record<string, unknown>): string {
  switch (type) {
    case 'timeseries': {
      const cols = (data?.cols ?? []) as Array<{ n: string; v: unknown[] }>
      const x = (data?.x ?? []) as unknown[]
      return `含 ${x.length} 个数据点、${cols.length} 条序列（${cols.map((c) => c.n).join(' / ')}）。`
    }
    case 'histogram': {
      const counts = (data?.counts ?? []) as number[]
      return `含 ${counts.length} 个分箱。`
    }
    case 'matrix': {
      const values = (data?.values ?? []) as number[][]
      return `含 ${values.length}×${values[0]?.length ?? 0} 的矩阵。`
    }
    case 'table': {
      const rows = (data?.rows ?? []) as unknown[]
      const columns = (data?.columns ?? []) as unknown[]
      return `含 ${rows.length} 行、${columns.length} 列。`
    }
    case 'metric': {
      const items = (data?.items ?? []) as Array<{ label: string }>
      return `含 ${items.length} 个指标（${items.map((i) => i.label).join(' / ')}）。`
    }
    default:
      return ''
  }
}
