/**
 * 节点注册中心 — 类型定义
 *
 * 统一的节点元数据描述，供 UI（组件面板/参数表单）和 AI（Tool Calling）共用。
 */

// === 参数 Schema ===

export type ParamType =
  | 'text' | 'select' | 'number' | 'boolean'
  | 'date' | 'daterange' | 'textarea'
  | 'multiselect' | 'multiselect-dropdown'
  | 'directory' | 'file' | 'config-select' | 'download'
  | 'button'

export interface ParamSchema {
  key: string             // 参数键（英文，用于后端传输）
  label: string           // 显示名称（中文）
  type: ParamType
  default?: any
  options?: Array<{ label: string; value: any }> | string[]
  min?: number
  max?: number
  step?: number | string  // 支持 number 或 'any' 字符串
  unit?: string
  visible?: boolean       // 默认是否可见
  placeholder?: string
  pattern?: string        // 正则验证
  errorMsg?: string       // 验证失败提示
  description?: string    // 参数说明（给 AI 看）
  dependsOn?: string      // 依赖的参数 key，当该参数取特定值时才显示
  dependsValue?: any      // 依赖参数的目标值
}

// === 节点分类 ===

export type NodeCategory =
  | 'input'       // 数据输入
  | 'process'     // 数据处理/指标计算
  | 'signal'      // 信号生成
  | 'execution'   // 交易执行
  | 'ml'          // 机器学习模型
  | 'risk'        // 风控
  | 'causal'      // 因果推理（HMM 市场状态识别）
  | 'utility'     // 工具（调试/测试）

// === 分类默认图标 ===

export const CATEGORY_ICONS: Record<NodeCategory, string> = {
  input:    'fas fa-plug',        // 输入：插头/数据源
  process:  'fas fa-cogs',        // 处理：齿轮/计算
  signal:   'fas fa-bolt',        // 信号：闪电
  execution:'fas fa-exchange-alt', // 执行：交换/交易
  ml:       'fas fa-brain',       // ML：大脑
  risk:     'fas fa-shield-alt',  // 风控：盾牌
  causal:   'fas fa-project-diagram', // 因果推理：网络图
  utility:  'fas fa-wrench',      // 工具：扳手
}

// === 节点注册表条目 ===

export interface NodeRegistryEntry {
  id: string              // 唯一标识，如 'data-source'
  label: string           // 显示名称，如 '数据输入'
  nodeType: string        // 后端类型标识，如 'input' / 'function' / 'signal'
  category: NodeCategory  // 分类
  description: string     // 功能描述（给 AI 和组件面板提示用）
  inputs: string[]        // 输入数据要求，如 ['timeseries', 'ohlcv']
  outputs: string[]       // 输出数据，如 ['ohlcv', 'indicator']
  params: ParamSchema[]   // 参数列表
  example: Record<string, any>  // 典型 params 示例（用于 AI 生成策略图）

  // 视觉配置（可选，不设置则使用分类默认值）
  color?: string          // 节点边框/头部颜色
  hasInput?: boolean      // 是否有左侧输入连接点（默认 true）
  hasOutput?: boolean     // 是否有右侧输出连接点（默认 true）
}

// === 节点实例数据（序列化到 JSON 的结构）===

export interface NodeInstanceData {
  label: string
  nodeType: string
  params: Record<string, {
    value: any
    type: string
    options?: any[]
    visible?: boolean
  }>
}
