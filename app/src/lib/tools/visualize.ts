/**
 * visualize 工具 —— UI 以 tool 的方式调用
 *
 * Agent 调一次这个工具，产出一个 `ui` 产物：payload 是一份平铺的 UiSpec，
 * 描述「画什么、怎么画」，由 UI 注册表分发到具体视图组件渲染。
 *
 * 两种工作模式：
 *   1. **数据驱动**（timeseries / histogram / matrix / table / metric）
 *      LLM 直接给 data → 按该类型的 zod schema 校验（工具约束）→ 出产物。
 *   2. **resolver 驱动**（performance / price）
 *      LLM 只给 params → 工具跑该类型的 resolver 拿 data + controls → 出产物。
 *      数据层控件（换基准、换标的）变更时，渲染层再跑一次 resolver 回源。
 *
 * 工具描述由注册表自动生成，保证「描述」与「实际约束」永不漂移。
 */

import { tool } from '@langchain/core/tools'
import { z } from 'zod'
import { getUiType, listUiTypes, mergeParams, registerUiType } from '@/lib/charts/registry'
import { getResolver } from '@/lib/charts/resolvers'
import { UI_TYPE_DEFS } from '@/lib/charts/uiTypes'
import { makeArtifact, type ToolArtifactResult } from '@/lib/agent/artifacts'

// 先登记契约，工具描述才能列全类型（组件由 UI 层另行补上，不进 LLM 调用链）
for (const def of UI_TYPE_DEFS) {
  registerUiType(def)
}

/** 生成工具描述里的类型清单 */
function buildTypeDescription(): string {
  return listUiTypes()
    .map((d) => {
      const mode = d.resolver ? '数据由系统出，你只需给 params' : '你直接给 data'
      return `- ${d.id}（${d.label}，${mode}）：data 形状 ${d.schemaDescription}`
    })
    .join('\n')
}

const TOOL_DESCRIPTION = `生成可视化产物（图表 / 表格 / 指标），直接呈现在对话流里。

**用法**：type 决定产物形状，data 或 params 按该类型的要求填。

${buildTypeDescription()}

**要点**
- data 用列式结构：每列是 { n: 列名, v: 值数组 }，名字与值绑定，不要用「数值矩阵 + 名字数组」分开写。
- 数值一律用「小数」表达（例如 0.0231 表示 2.31%），percent 格式由 y_format 指定。
- 时间戳用 Unix 秒（10 位）。
- 表格用 columns（列定义）+ rows（对象数组）两段，不要自己拼 HTML 或 markdown 表格。
- 画完之后可以在回复里顺带解读图表结论。`

export const visualizeTool = tool(
  async (params) => {
    const { type, title, subtitle, source, params: uiParams, data } = params

    const def = getUiType(type)
    if (!def) {
      const known = listUiTypes().map((d) => d.id).join(' / ')
      return `错误：未知的 UI 类型「${type}」。可选：${known}`
    }

    const mergedParams = mergeParams(def, uiParams ?? {})

    /* ---------- 模式 2：resolver 驱动 ---------- */
    if (def.resolver) {
      const resolver = getResolver(def.resolver)
      if (!resolver) {
        return `错误：UI 类型「${type}」声明了 resolver「${def.resolver}」，但它没有注册。`
      }
      try {
        const out = await resolver(mergedParams)
        const spec = {
          type: def.id,
          title,
          subtitle,
          source,
          params: mergedParams,
          data: out.data,
          controls: out.controls ?? def.controls,
        }
        const artifact = makeArtifact('ui', title, spec, {
          subtitle,
          source: source ?? `visualize · ${def.id}`,
        })
        return {
          text: `已生成「${title}」（${def.label}），数据由系统回源取得。用户在产物卡上可切换参数。`,
          artifact,
        } as ToolArtifactResult
      } catch (e) {
        const message = e instanceof Error ? e.message : String(e)
        return `生成「${title}」失败：${message}`
      }
    }

    /* ---------- 模式 1：数据驱动 ---------- */
    const parsed = def.schema.safeParse(data)
    if (!parsed.success) {
      // 把期望形状与具体错误回给 LLM，让它自己改
      const issues = parsed.error.issues
        .slice(0, 6)
        .map((i) => `  - ${i.path.join('.') || '(根)'}：${i.message}`)
        .join('\n')
      return [
        `data 不符合「${def.id}」的要求，未生成产物。`,
        ``,
        `期望的 data 形状：${def.schemaDescription}`,
        ``,
        `具体问题：`,
        issues,
      ].join('\n')
    }

    const spec = {
      type: def.id,
      title,
      subtitle,
      source,
      params: mergedParams,
      data: parsed.data as Record<string, unknown>,
      controls: def.controls,
    }

    const artifact = makeArtifact('ui', title, spec, {
      subtitle,
      source: source ?? `visualize · ${def.id}`,
    })

    const summary = summarize(def.id, parsed.data as Record<string, unknown>)
    return {
      text: `已生成「${title}」（${def.label}）。${summary}`,
      artifact,
    } as ToolArtifactResult
  },
  {
    name: 'visualize',
    description: TOOL_DESCRIPTION,
    schema: z.object({
      type: z
        .string()
        .describe('UI 类型 id，决定产物的形状要求。可选：' + listUiTypes().map((d) => d.id).join(' / ')),
      title: z.string().describe('产物标题，如「策略累计收益 vs 沪深300」'),
      subtitle: z.string().optional().describe('副标题'),
      source: z.string().optional().describe('数据来源说明，如 backtest:v3'),
      params: z
        .record(z.string(), z.any())
        .optional()
        .describe('参数。resolver 驱动的类型用这里传（如 benchmark 基准代码）；其余类型可留空'),
      data: z.any().optional().describe('数据。resolver 驱动的类型不需要给；其余类型按该类型的 data 形状给'),
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
