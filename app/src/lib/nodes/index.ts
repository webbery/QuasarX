/**
 * 节点配置 — 统一入口
 *
 * 导入此文件时自动注册所有节点类型。
 * 新增节点：创建 configs/xxx.ts → 在此处加一行 import 即可。
 */

import './configs/input'
import './configs/function'
import './configs/signal'
import './configs/execution'
import './configs/risk'
import './configs/spread'
import './configs/portfolio'
import './configs/ml'
import './configs/utility'

export { getNode, getAllNodes, getNodesByCategory, getAllCategories, searchNodes, getNodeIcon, getNodeColor, convertLabelsToKeys, convertKeysToLabels } from './registry'
export { CATEGORY_ICONS } from './types'
export type { NodeRegistryEntry, ParamSchema, NodeCategory } from './types'
