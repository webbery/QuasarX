import { getAllNodes, getNode } from './registry'
import type {
  CreateStrategyNodeOptions,
  NodeParamConfig,
  NodeRegistryEntry,
  ParamSchema,
  ParamType,
  StrategyFlowNode,
} from './types'

function cloneValue<T>(value: T): T {
  return value === undefined ? value : structuredClone(value)
}

function isRecord(value: unknown): value is Record<string, any> {
  return typeof value === 'object' && value !== null && !Array.isArray(value)
}

function inferParamType(value: unknown): ParamType {
  if (typeof value === 'boolean') return 'boolean'
  if (typeof value === 'number') return 'number'
  return 'text'
}

function resolveNodeEntry(options: CreateStrategyNodeOptions): NodeRegistryEntry {
  const entryById = options.registryId ? getNode(options.registryId) : undefined
  if (entryById && options.nodeType && entryById.nodeType !== options.nodeType) {
    throw new Error(`节点类型不匹配: registryId=${options.registryId}, nodeType=${options.nodeType}`)
  }

  const entry = entryById || getAllNodes().find(node => node.nodeType === options.nodeType)
  if (!entry) {
    const identifier = options.registryId || options.nodeType || ''
    throw new Error(`未找到节点类型配置: ${identifier}`)
  }
  return entry
}

function createParamConfig(schema: ParamSchema, suppliedValue: unknown, hasSuppliedValue: boolean): NodeParamConfig {
  const suppliedConfig = isRecord(suppliedValue) && 'value' in suppliedValue
    ? suppliedValue
    : undefined

  return {
    value: cloneValue(hasSuppliedValue
      ? suppliedConfig ? suppliedConfig.value : suppliedValue
      : schema.default),
    type: schema.type,
    label: schema.label,
    ...(schema.options && { options: cloneValue(schema.options) }),
    visible: suppliedConfig?.visible ?? schema.visible ?? true,
    ...(schema.placeholder && { placeholder: schema.placeholder }),
    ...(schema.pattern && { pattern: schema.pattern }),
    ...(schema.errorMsg && { errorMsg: schema.errorMsg }),
    ...(schema.min !== undefined && { min: schema.min }),
    ...(schema.max !== undefined && { max: schema.max }),
    ...(schema.step !== undefined && { step: schema.step }),
    ...(schema.unit && { unit: schema.unit }),
  }
}

function createUnknownParamConfig(value: unknown): NodeParamConfig {
  if (isRecord(value) && 'value' in value) {
    return {
      ...cloneValue(value),
      value: cloneValue(value.value),
      type: typeof value.type === 'string' ? value.type as ParamType : inferParamType(value.value),
      visible: typeof value.visible === 'boolean' ? value.visible : false,
    }
  }

  return {
    value: cloneValue(value),
    type: inferParamType(value),
    visible: false,
  }
}

export function createStrategyNode(options: CreateStrategyNodeOptions): StrategyFlowNode {
  if (options.id === undefined || options.id === null || String(options.id).trim() === '') {
    throw new Error('节点 id 不能为空')
  }
  const id = String(options.id)
  if (!options.position || !Number.isFinite(options.position.x) || !Number.isFinite(options.position.y)) {
    throw new Error(`节点 ${id} 的 position 无效`)
  }

  const entry = resolveNodeEntry(options)
  const suppliedParams = options.params || {}
  const consumedKeys = new Set<string>()
  const params: Record<string, NodeParamConfig> = {}

  for (const schema of entry.params) {
    const labelExists = Object.prototype.hasOwnProperty.call(suppliedParams, schema.label)
    const keyExists = Object.prototype.hasOwnProperty.call(suppliedParams, schema.key)
    const suppliedKey = labelExists ? schema.label : keyExists ? schema.key : undefined

    if (suppliedKey) consumedKeys.add(suppliedKey)
    params[schema.key] = createParamConfig(
      schema,
      suppliedKey ? suppliedParams[suppliedKey] : undefined,
      suppliedKey !== undefined,
    )
  }

  for (const [key, value] of Object.entries(suppliedParams)) {
    if (consumedKeys.has(key)) continue
    console.warn(`[createStrategyNode] 节点 ${id} (${entry.nodeType}) 包含未注册参数: ${key}`)
    params[key] = createUnknownParamConfig(value)
  }

  return {
    id,
    type: 'custom',
    position: cloneValue(options.position),
    data: {
      label: options.label || entry.label,
      nodeType: entry.nodeType,
      params,
    },
  }
}
