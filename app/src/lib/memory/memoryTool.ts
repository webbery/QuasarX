/**
 * 记忆工具（常驻）
 *
 * memory(action="recall",  keyword="<查询>")   取回相关记忆
 * memory(action="reflect", keyword="<问题>")   取回带 id 的证据块，由当前 agent 作答
 * memory(action="retain",  keyword="<要点>")   显式写入一条事实
 * memory(action="forget",  keyword="<记忆 id>") 遗忘
 *
 * store 由调用方注入。
 */

import { tool } from '@langchain/core/tools'
import type { StructuredToolInterface } from '@langchain/core/tools'
import { z } from 'zod'

import type { MemoryStore, MemoryUnit } from './types'
import { recall } from './recall'
import { renderMemoryBlock, MEMORY_INJECT_TOKENS } from './budget'
import { buildEmbedText } from './embed'

/** 工具依赖 */
export interface MemoryToolDeps {
  /** 记忆存储 */
  store: MemoryStore
  /** 当前 bank */
  bankId: string
  /** 记忆开关；关掉时工具直接返回提示，不做任何读写 */
  enabled?: boolean
}

/** 证据块渲染：带 id，供引用校验 */
function renderEvidence(units: MemoryUnit[]): string {
  return units
    .map((u, i) => `[${i + 1}] (id=${u.id}) ${u.text}`)
    .join('\n')
}

/** 显式写入的记忆单元 */
function manualUnit(keyword: string, bankId: string): MemoryUnit {
  const now = Date.now()
  const suffix = Math.random().toString(36).slice(2, 8)
  return {
    id: `mem_manual_${now}_${suffix}`,
    bankId,
    text: keyword.trim(),
    embedText: buildEmbedText(keyword.trim(), null),
    factType: 'world',
    occurredStart: null,
    occurredEnd: null,
    mentionedAt: now,
    confidence: null,
    entities: [],
    // 幂等键必须逐条唯一：同一毫秒内的多次写入不能互相顶掉
    sourceTurnId: `manual:${now}_${suffix}`,
    accessCount: 0,
    context: 'manual',
  }
}

/**
 * 创建记忆工具
 */
export function createMemoryTool(deps: MemoryToolDeps): StructuredToolInterface {
  return tool(
    async ({ action, keyword, budget }) => {
      if (deps.enabled === false) {
        return '记忆功能当前关闭，未执行任何读写。'
      }
      const query = (keyword ?? '').trim()
      const maxTokens = budget && budget > 0 ? budget : MEMORY_INJECT_TOKENS

      try {
        if (action === 'recall') {
          if (!query) return '缺少查询词，未执行召回。'
          const result = await recall(deps.store, {
            bankId: deps.bankId,
            query,
            maxTokens,
          })
          if (!result.units.length) return '没有召回到相关记忆。'
          const rendered = renderMemoryBlock(result.units, maxTokens)
          return [
            `召回到 ${result.units.length} 条记忆（${rendered.packed.totalTokens} token）：`,
            rendered.text,
          ].join('\n')
        }

        if (action === 'reflect') {
          if (!query) return '缺少问题，未执行取证。'
          const result = await recall(deps.store, {
            bankId: deps.bankId,
            query,
            maxTokens,
          })
          if (!result.units.length) return '没有召回到可引用的记忆。'
          return [
            '以下是与问题相关的记忆，回答时只能引用这些 id：',
            renderEvidence(result.units),
          ].join('\n')
        }

        if (action === 'retain') {
          if (!query) return '缺少要点，未写入。'
          const unit = manualUnit(query, deps.bankId)
          await deps.store.upsert([unit])
          return `已写入 1 条记忆（id=${unit.id}）。`
        }

        // forget
        if (!query) return '缺少记忆 id，未执行遗忘。'
        if (query === 'all') {
          const deleted = await deps.store.deleteByBank(deps.bankId)
          return `已清除当前作用域的 ${deleted} 条记忆。`
        }
        // 记忆 id 与来源轮次 id 都能用来删：先按 id 查，命中不了再按来源轮次删
        const byId = await deps.store.getByIds([query])
        const sourceTurnId = byId[0]?.sourceTurnId ?? query
        const deleted = await deps.store.deleteBySource(sourceTurnId, deps.bankId)
        if (!deleted) return `没有找到 id 为「${query}」的记忆。`
        return `已遗忘 ${deleted} 条记忆。`
      } catch (err) {
        return `记忆操作失败：${err instanceof Error ? err.message : String(err)}`
      }
    },
    {
      name: 'memory',
      description:
        '读写当前会话的记忆。action=recall 取回与关键词相关的记忆；action=reflect 取回带 id 的证据块用于作答；' +
        'action=retain 显式写入一条要点；action=forget 按记忆 id 遗忘（keyword 传 all 清除当前作用域全部记忆）。',
      schema: z.object({
        action: z
          .enum(['recall', 'reflect', 'retain', 'forget'])
          .describe('要执行的动作：recall 召回 / reflect 取证 / retain 写入 / forget 遗忘'),
        keyword: z.string().describe('召回或写入的关键词、问题、要点、记忆 id'),
        budget: z.number().optional().describe('召回的 token 上限，默认 1500'),
      }),
    },
  )
}
