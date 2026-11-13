/**
 * 产物渲染注册表
 *
 * 把「产物类型」与「渲染器」解耦：
 * - 渲染层按 kind 查表拿组件，不需要为每种新产物改一次分发逻辑
 * - 未登记的 kind 走兜底组件（显示原始 payload），对话流不会崩
 *
 * 新增一种产物的完整步骤：
 *   1. 在 artifacts.ts 定义 payload 契约
 *   2. 写一个渲染组件
 *   3. registerArtifact({ kind, label, render })
 *   4. 工具返回 { text, artifact: makeArtifact(kind, ...) }
 */

import type { Component } from 'vue'
import type { ArtifactEnvelope, ArtifactKind } from './artifacts'

export interface ArtifactSpec<TPayload = any> {
  kind: ArtifactKind
  /** 显示名称 */
  label: string
  icon?: string
  /** 载荷契约（工具层用它自检 payload） */
  schema?: unknown
  /** 渲染组件 */
  render: Component
  /** 是否支持紧凑形态 */
  compact?: boolean
  /** 是否支持导出 */
  exportable?: boolean
}

const specs = new Map<string, ArtifactSpec>()

/** 注册一种产物类型 */
export function registerArtifact(spec: ArtifactSpec): void {
  specs.set(spec.kind, spec)
}

/** 按 kind 查找渲染规格 */
export function getArtifactSpec(kind: string): ArtifactSpec | undefined {
  return specs.get(kind)
}

/** 已登记的所有 kind */
export function listArtifactKinds(): string[] {
  return [...specs.keys()]
}

/** 已登记的规格列表 */
export function listArtifactSpecs(): ArtifactSpec[] {
  return [...specs.values()]
}

/**
 * 兜底渲染：当 kind 未登记时，把 payload 原样展示，
 * 保证 Agent 产出未知类型时对话流仍可读。
 */
export function describeUnregisteredArtifact(artifact: ArtifactEnvelope): string {
  try {
    return JSON.stringify(artifact.payload, null, 2)
  } catch {
    return String(artifact.payload)
  }
}
