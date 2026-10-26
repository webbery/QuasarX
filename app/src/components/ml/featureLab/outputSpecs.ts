// 配方节点 → 输出端点规格
//
// 端点命名与后端 out_elements() 对齐：
//   function / formula : <symbol>.<label>
//   emd                : <symbol>.<label>.nimf_i / .energy_velocity / .volume_regime
// 门禁侧会剥掉 <symbol>. 前缀，剩下的是这里返回的短名。

import type { OutputEndpoint, Recipe, RecipeNodeType } from './types'
import { newId } from './types'

export interface FunctionSlot {
  slot: string
  field: string
}

/**
 * 指标方法 → 输入槽位。
 *
 * 注意：ATR 的 callable 从 args["close"] 取收盘价，但 FunctionNode::Process 会把
 * sourceHandle 里的 "close" 统一改名成 "price"（FunctionNode.cpp:240），导致 ATR
 * 永远拿不到 close、输出全是 NaN，因此本期不提供 ATR。
 */
export const FUNCTION_METHODS: Record<string, FunctionSlot[]> = {
  MA: [{ slot: 'price', field: 'close' }],
  STD: [{ slot: 'price', field: 'close' }],
  Return: [{ slot: 'price', field: 'close' }],
  R2: [{ slot: 'price', field: 'close' }],
  ZScore: [{ slot: 'price', field: 'close' }],
  Median: [{ slot: 'price', field: 'close' }],
  CumMax: [{ slot: 'price', field: 'close' }],
  VPCorr: [
    { slot: 'price', field: 'close' },
    { slot: 'volume', field: 'volume' },
  ],
}

export const FUNCTION_METHOD_OPTIONS = Object.keys(FUNCTION_METHODS)

export const EMD_METHOD_OPTIONS = ['emd', 'ceemdan', 'vmd'] as const

export const EMD_INPUT_FIELDS = ['close', 'volume'] as const

export const RAW_FIELDS = ['close', 'open', 'high', 'low', 'volume', 'turnover'] as const

/** 公式里不能当成变量名的标识符 */
export const FORMULA_KEYWORDS = new Set([
  'and', 'or', 'not', 'true', 'false', 'True', 'False',
  'topk', 'rank', 'pct', 'zscore', 't',
])

export const RESERVED_NAMES = new Set<string>([
  ...RAW_FIELDS,
  ...FORMULA_KEYWORDS,
  'date',
  'label',
  'energy_velocity',
  'volume_regime',
])

const NAME_RE = /^[A-Za-z_][A-Za-z0-9_]*$/

/** 返回错误文案；合法时返回 null */
export function validateRecipeName(name: string, others: string[]): string | null {
  if (!name) return '名称不能为空'
  if (!NAME_RE.test(name)) return '只能用字母/数字/下划线，且不能以数字开头'
  if (RESERVED_NAMES.has(name)) return `"${name}" 是保留字段名`
  if (/^nimf_\d+$/.test(name)) return '"nimf_N" 是 EMD 端点的保留名'
  if (others.includes(name)) return '名称重复'
  return null
}

export function uniqueName(base: string, existing: string[]): string {
  if (!existing.includes(base)) return base
  let i = 2
  while (existing.includes(`${base}${i}`)) i++
  return `${base}${i}`
}

export function resolveOutputs(recipe: Recipe): OutputEndpoint[] {
  const name = recipe.name || 'feature'
  if (recipe.nodeType === 'function' || recipe.nodeType === 'formula') {
    return [{ name, label: name, formulaRef: true }]
  }

  const out: OutputEndpoint[] = []
  const n = clampInt(recipe.params?.numIMFs, 1, 20, 5)
  for (let i = 0; i < n; i++) {
    out.push({ name: `${name}.nimf_${i}`, label: `nimf_${i}`, formulaRef: false })
  }
  if (recipe.params?.computeEnergyVelocity) {
    out.push({ name: `${name}.energy_velocity`, label: 'energy_velocity', formulaRef: false })
  }
  if (recipe.params?.computeVolumeRegime) {
    out.push({ name: `${name}.volume_regime`, label: 'volume_regime', formulaRef: false })
  }
  return out
}

/** EMD 衍生端点只允许在分解 volume 时开启（后端从同 symbol 的 volume 列读） */
export function canComputeVolumeRegime(recipe: Recipe): boolean {
  return recipe.nodeType === 'emd' && recipe.params?.inputField === 'volume'
}

export function createRecipe(nodeType: RecipeNodeType, existing: Recipe[]): Recipe {
  const names = existing.map(r => r.name)
  if (nodeType === 'function') {
    const method = 'MA'
    const range = 20
    return {
      id: newId('recipe'),
      name: uniqueName(`ma${range}`, names),
      nodeType,
      params: { method, range },
      selectedOutputs: [`ma${range}`],
    }
  }
  if (nodeType === 'formula') {
    let i = 1
    while (names.includes(`expr${i}`)) i++
    return {
      id: newId('recipe'),
      name: `expr${i}`,
      nodeType,
      params: { expression: '' },
      selectedOutputs: [`expr${i}`],
    }
  }
  let i = 1
  while (names.includes(`emd${i}`)) i++
  return {
    id: newId('recipe'),
    name: `emd${i}`,
    nodeType: 'emd',
    params: {
      method: 'emd',
      numIMFs: 5,
      windowSize: 120,
      inputField: 'close',
      computeEnergyVelocity: true,
      computeVolumeRegime: false,
      ensembles: 50,
      noiseStd: 0.2,
      alpha: 2000,
      tau: 0,
      tol: 1e-6,
    },
    selectedOutputs: [],
  }
}

export function clampInt(v: any, min: number, max: number, fallback: number): number {
  const n = Math.round(Number(v))
  if (!Number.isFinite(n)) return fallback
  return Math.min(Math.max(n, min), max)
}
