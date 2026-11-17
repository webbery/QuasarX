/**
 * UI 类型注册表
 *
 * 每个 UI 类型声明：输入约束（工具约束）+ 参数控件 + 渲染组件 + 数据层 resolver。
 *
 * 新增一种 UI 类型的完整步骤：
 *   1. 在 types.ts 的 UI_TYPE 里登记 id
 *   2. 写 zod schema（data 的形状约束）与 schemaDescription（给 LLM 看的说明）
 *   3. 写渲染组件（收 spec + params + density）
 *   4. 在 registerUiTypes.ts 里 registerUiType({...})
 *   5. （可选）在 resolvers.ts 里 registerResolver 供数据层控件回源
 *
 * visualize 工具不需要改 —— 它的描述与校验都从注册表生成。
 */

import type { UiTypeDefinition, UiTypeId } from './types'

const defs = new Map<string, UiTypeDefinition>()

/** 注册一种 UI 类型 */
export function registerUiType(def: UiTypeDefinition): void {
  defs.set(def.id, def)
}

/** 按 id 查找 UI 类型定义 */
export function getUiType(id: string): UiTypeDefinition | undefined {
  return defs.get(id)
}

/** 已登记的所有 UI 类型 */
export function listUiTypes(): UiTypeDefinition[] {
  return [...defs.values()]
}

/** 已登记的 UI 类型 id */
export function listUiTypeIds(): string[] {
  return [...defs.keys()]
}

/**
 * 生成给 LLM 看的数据形状说明 —— 工具描述由它拼出来，
 * 保证「描述」与「实际约束」永远一致。
 */
export function describeUiTypes(): string {
  return listUiTypes()
    .map((d) => `- ${d.id}（${d.label}）：${d.schemaDescription}`)
    .join('\n')
}

/** 生成 type 枚举的可读列表 */
export function describeUiTypeEnum(): string {
  return listUiTypes()
    .map((d) => `${d.id}=${d.label}`)
    .join(' / ')
}

/** 把某类型的默认参数与用户参数合并（用户参数优先） */
export function mergeParams(
  def: UiTypeDefinition | undefined,
  params: Record<string, unknown> | undefined,
): Record<string, unknown> {
  const base = { ...(def?.defaults ?? {}) }
  for (const c of def?.controls ?? []) {
    if (c.default !== undefined && base[c.key] === undefined) {
      base[c.key] = c.default
    }
  }
  return { ...base, ...(params ?? {}) }
}
