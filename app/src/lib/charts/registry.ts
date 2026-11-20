/**
 * UI 类型注册表（转发）
 *
 * 实现已经搬到 `@/lib/ui/registry` —— 注册表本身与「图表」无关。
 * 这里保留一层转发，是为了不打断现有从 `@/lib/charts/registry` 导入的调用方。
 */

export * from '@/lib/ui/registry'
