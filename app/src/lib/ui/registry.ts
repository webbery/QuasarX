/**
 * UI 类型注册表（通用层）
 *
 * 每个 UI 类型声明：输入约束（工具约束）+ 参数控件 + 渲染组件 + 数据层 resolver。
 *
 * 新增一种 UI 类型的完整步骤：
 *   1. 在 protocol.ts 的 UI_TYPE 里登记 id
 *   2. 写 zod schema（input 的形状约束）与 schemaDescription（给 LLM 看的说明）
 *   3. 写渲染组件（收 data + params + density）
 *   4. 在 registerUiTypes.ts 里登记组件
 *   5. （可选）在 charts/resolvers.ts 里 registerResolver 供数据层控件回源
 *
 * ui 工具不需要改 —— 它的 builder 列表与校验都从这张表生成。
 */

import type { Component } from 'vue'
import type { UiTypeDefinition } from './protocol'

const defs = new Map<string, UiTypeDefinition>()

/**
 * 注册一种 UI 类型。
 *
 * 契约（schema / controls / resolver）与组件分开注册时，后注册的会**合并**而不是覆盖 ——
 * 工具层先注册契约、UI 层后补组件，两边都调这个函数也不会互相抹掉。
 */
export function registerUiType(def: UiTypeDefinition): void {
  const existing = defs.get(def.id)
  if (!existing) {
    defs.set(def.id, { ...def })
    return
  }
  defs.set(def.id, {
    ...existing,
    ...def,
    // 后注册没带组件时，保留先注册的那个
    component: def.component ?? existing.component ?? null,
  })
}

/** 给已登记的 UI 类型补上渲染组件 */
export function attachUiComponent(id: string, component: Component): void {
  const existing = defs.get(id)
  if (!existing) return
  defs.set(id, { ...existing, component })
}

/** 按 id 查找 UI 类型定义 */
export function getUiType(id: string): UiTypeDefinition | undefined {
  return defs.get(id)
}

/** 已登记的所有 UI 类型 */
export function listUiTypes(): UiTypeDefinition[] {
  return [...defs.values()]
}

/** 已登记、且允许 LLM 构建的 UI 类型（排除 progress 这类内部类型） */
export function listBuildableUiTypes(): UiTypeDefinition[] {
  return [...defs.values()].filter((d) => !d.hidden)
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
  return listBuildableUiTypes()
    .map((d) => `- ${d.id}（${d.label}）：${d.schemaDescription}`)
    .join('\n')
}

/** 生成 type 枚举的可读列表 */
export function describeUiTypeEnum(): string {
  return listBuildableUiTypes()
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
