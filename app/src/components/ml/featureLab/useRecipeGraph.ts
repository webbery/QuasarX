// 配方集 → 合成策略图 / 校验 / 收集结果聚合
//
// 合成图的最小形态：
//   input(1) ──> 各配方节点(2..N+1) ──> xgboost 锚点(N+2)
// xgboost 只作为 collect 反向遍历的根（MLHandler.cpp:403），不参与特征筛选。
// 所有配方节点都连到锚点，因此一次收集会产出全部端点列。

import type { FeatureSnapshot, EndpointStats, Recipe, RecipeSet } from './types'
import { newId } from './types'
import type { FeatureReport } from '../composables/useMLState'
import { toGraphSymbols } from '@/lib/symbol'
import {
  FUNCTION_METHODS,
  EMD_METHOD_OPTIONS,
  EMD_INPUT_FIELDS,
  RAW_FIELDS,
  FORMULA_KEYWORDS,
  RESERVED_NAMES,
  resolveOutputs,
  validateRecipeName,
  clampInt,
} from './outputSpecs'

const ANCHOR_LABEL = 'anchor'

interface GraphEdge {
  id: string
  source: string
  target: string
  sourceHandle: string
  targetHandle: string
  type: string
}

function pv(value: any, type = 'text') {
  return { value, type }
}

function makeNode(id: number, label: string, nodeType: string, params: Record<string, any>) {
  return {
    id: String(id),
    type: 'custom',
    position: { x: 0, y: 0 },
    data: { label, nodeType, params },
  }
}

function makeEdge(source: number, target: number, sourceHandle: string, targetHandle: string): GraphEdge {
  return {
    id: `e_${source}_${target}_${sourceHandle}_${targetHandle}`,
    source: String(source),
    target: String(target),
    sourceHandle,
    targetHandle,
    type: 'default',
  }
}

/** 公式里的变量名（去掉函数名、关键字与时间下标 t） */
export function extractFormulaIdentifiers(expr: string): string[] {
  const out = new Set<string>()
  const re = /[A-Za-z_][A-Za-z0-9_]*/g
  let m: RegExpExecArray | null
  while ((m = re.exec(expr || ''))) {
    if (!FORMULA_KEYWORDS.has(m[0])) out.add(m[0])
  }
  return [...out]
}

export interface RecipeIssue {
  recipeId?: string
  message: string
}

/** 建图前校验；返回空数组表示可以收集 */
export function validateRecipeSet(set: RecipeSet, minNames: number): RecipeIssue[] {
  const issues: RecipeIssue[] = []
  if (!set.name.trim()) issues.push({ message: '配方集名称不能为空' })
  if (set.universe.length < Math.max(1, minNames)) {
    issues.push({ message: `标的池至少需要 ${minNames} 只，当前 ${set.universe.length} 只` })
  }
  if (set.recipes.length === 0) {
    issues.push({ message: '至少添加一个配方节点' })
  }

  const names = set.recipes.map(r => r.name)
  const seen = new Set<string>()
  let selectedTotal = 0

  for (const r of set.recipes) {
    const others = names.filter(n => n !== r.name)
    const nameErr = validateRecipeName(r.name, others)
    if (nameErr) issues.push({ recipeId: r.id, message: `名称 ${r.name || '(空)'}：${nameErr}` })
    if (seen.has(r.name)) issues.push({ recipeId: r.id, message: `名称 ${r.name} 重复` })
    seen.add(r.name)

    const outputs = resolveOutputs(r)
    const outputNames = new Set(outputs.map(o => o.name))
    for (const ep of r.selectedOutputs) {
      if (!outputNames.has(ep)) {
        issues.push({ recipeId: r.id, message: `端点 ${ep} 在当前参数下不存在` })
      } else {
        selectedTotal++
      }
    }

    if (r.nodeType === 'function') {
      const method = r.params?.method
      if (!FUNCTION_METHODS[method]) {
        issues.push({ recipeId: r.id, message: `未知指标方法 ${method}` })
      }
      const range = Number(r.params?.range)
      if (!Number.isInteger(range) || range < 1 || range > 1000) {
        issues.push({ recipeId: r.id, message: '窗口 range 必须是 1~1000 的整数' })
      }
    }

    if (r.nodeType === 'formula') {
      const expr = String(r.params?.expression || '').trim()
      if (!expr) {
        issues.push({ recipeId: r.id, message: '公式不能为空' })
      } else {
        const prior = new Set(
          set.recipes.slice(0, set.recipes.indexOf(r)).map(x => x.name),
        )
        for (const id of extractFormulaIdentifiers(expr)) {
          if ((RAW_FIELDS as readonly string[]).includes(id)) continue
          if (!prior.has(id)) {
            issues.push({ recipeId: r.id, message: `公式变量 "${id}" 未在上方定义` })
          }
        }
      }
    }

    if (r.nodeType === 'emd') {
      if (!(EMD_METHOD_OPTIONS as readonly string[]).includes(r.params?.method)) {
        issues.push({ recipeId: r.id, message: `未知 EMD 算法 ${r.params?.method}` })
      }
      if (!(EMD_INPUT_FIELDS as readonly string[]).includes(r.params?.inputField)) {
        issues.push({ recipeId: r.id, message: 'EMD 输入字段必须是 close 或 volume' })
      }
      const n = Number(r.params?.numIMFs)
      if (!Number.isInteger(n) || n < 1 || n > 20) {
        issues.push({ recipeId: r.id, message: 'IMF 数量必须是 1~20 的整数' })
      }
      const w = Number(r.params?.windowSize)
      if (!(w === 0 || w >= 30)) {
        issues.push({ recipeId: r.id, message: '滚动窗口必须是 0 或 ≥30' })
      }
      if (r.params?.computeVolumeRegime && r.params?.inputField !== 'volume') {
        issues.push({ recipeId: r.id, message: '成交量体制仅在分解 volume 时可用' })
      }
    }
  }

  if (selectedTotal === 0) {
    issues.push({ message: '至少勾选一个输出端点' })
  }
  return issues
}

export function selectedFeatures(set: RecipeSet): string[] {
  const all = set.recipes.flatMap(r => resolveOutputs(r).map(o => o.name))
  const picked = set.recipes.flatMap(r => r.selectedOutputs)
  return picked.filter(p => all.includes(p))
}

export function estimateColumns(set: RecipeSet): number {
  const endpoints = set.recipes.reduce((n, r) => n + resolveOutputs(r).length, 0)
  return set.universe.length * endpoints
}

/**
 * 生成 collect 用的策略图 JSON 字符串。
 * 频率只影响 input 节点的 freq；collect 服务端不读 date_range.frequency。
 */
export function buildRecipeGraph(set: RecipeSet, frequency: string): string {
  const nodes: any[] = []
  const edges: GraphEdge[] = []

  // 1 = 行情输入（裸代码在这里统一补交易所前缀）
  nodes.push(makeNode(1, '行情数据', 'input', {
    source: pv('股票'),
    code: pv(toGraphSymbols(set.universe)),
    freq: pv(frequency, 'select'),
    close: pv('close'),
    open: pv('open'),
    high: pv('high'),
    low: pv('low'),
    volume: pv('volume'),
  }))

  const idByName = new Map<string, number>()
  const order: Recipe[] = [...set.recipes]

  order.forEach((recipe, idx) => {
    const id = idx + 2
    idByName.set(recipe.name, id)

    if (recipe.nodeType === 'function') {
      const method = FUNCTION_METHODS[recipe.params?.method] ? recipe.params.method : 'MA'
      nodes.push(makeNode(id, recipe.name, 'function', {
        method: pv(method, 'select'),
        range: pv(clampInt(recipe.params?.range, 1, 1000, 20), 'number'),
      }))
      for (const slot of FUNCTION_METHODS[method]) {
        edges.push(makeEdge(1, id, `1-${slot.field}`, `input-${slot.slot}`))
      }
      return
    }

    if (recipe.nodeType === 'formula') {
      const expr = String(recipe.params?.expression || '')
      nodes.push(makeNode(id, recipe.name, 'formula', {
        expression: pv(expr),
      }))
      for (const ref of extractFormulaIdentifiers(expr)) {
        if ((RAW_FIELDS as readonly string[]).includes(ref)) {
          edges.push(makeEdge(1, id, `1-${ref}`, String(id)))
        } else {
          const src = idByName.get(ref)
          if (src != null) edges.push(makeEdge(src, id, String(src), String(id)))
        }
      }
      return
    }

    // emd
    const p: Record<string, any> = {
      method: pv(recipe.params?.method || 'emd', 'select'),
      numIMFs: pv(clampInt(recipe.params?.numIMFs, 1, 20, 5), 'number'),
      windowSize: pv(clampInt(recipe.params?.windowSize, 0, 500, 120), 'number'),
      energyVelocityLabel: pv(recipe.params?.computeEnergyVelocity ? 'energy_velocity' : ''),
      volumeRegimeLabel: pv(recipe.params?.computeVolumeRegime ? 'volume_regime' : ''),
    }
    if (recipe.params?.method === 'ceemdan') {
      p.ensembles = pv(clampInt(recipe.params?.ensembles, 10, 200, 50), 'number')
      p.noiseStd = pv(Number(recipe.params?.noiseStd) || 0.2, 'number')
    } else if (recipe.params?.method === 'vmd') {
      p.alpha = pv(Number(recipe.params?.alpha) || 2000, 'number')
      p.tau = pv(Number(recipe.params?.tau) || 0, 'number')
      p.tol = pv(Number(recipe.params?.tol) || 1e-6, 'number')
    }
    nodes.push(makeNode(id, recipe.name, 'emd', p))
    const field = recipe.params?.inputField === 'volume' ? 'volume' : 'close'
    edges.push(makeEdge(1, id, `1-${field}`, 'input-timeseries'))
  })

  // xgboost 锚点
  const anchorId = order.length + 2
  nodes.push(makeNode(anchorId, ANCHOR_LABEL, 'xgboost', {
    features: pv(''),
  }))
  order.forEach((recipe, idx) => {
    const id = idx + 2
    edges.push(makeEdge(id, anchorId, String(id), String(anchorId)))
  })

  return JSON.stringify({ nodes, edges })
}

/**
 * 把 collect 的 FeatureReport 聚合成端点级统计。
 *
 * report.features[].name 是完整列名（如 sh.600000.emd.nimf_0），
 * 因此用 <symbol>.<端点短名> 做精确匹配，避免后缀误匹配。
 */
export function aggregateReport(
  report: FeatureReport,
  recipes: Recipe[],
  universe: string[],
): Pick<FeatureSnapshot, 'collectedOutputs' | 'features'> {
  const byName = new Map(report.features.map(f => [f.name, f]))
  const features: Record<string, EndpointStats> = {}
  const collectedOutputs: string[] = []
  const symbols = toGraphSymbols(universe)

  for (const recipe of recipes) {
    for (const ep of resolveOutputs(recipe)) {
      let columns = 0
      let validSum = 0
      let totalSum = 0
      let nanSum = 0
      let nanCount = 0

      for (const sym of symbols) {
        const stat = byName.get(`${sym}.${ep.name}`)
        if (!stat) continue
        columns++
        const total = stat.valid + stat.nan_count
        validSum += stat.valid
        totalSum += total
        if (Number.isFinite(stat.nan_pct)) {
          nanSum += stat.nan_pct
          nanCount++
        }
      }

      if (columns === 0) continue
      collectedOutputs.push(ep.name)
      features[ep.name] = {
        columns,
        dataCoverage: totalSum > 0 ? validSum / totalSum : 0,
        nanPct: nanCount > 0 ? nanSum / nanCount : 0,
      }
    }
  }

  return { collectedOutputs, features }
}

/** 按快照重建一份可编辑的配方集（用于「载入」） */
export function recipeSetFromSnapshot(
  snapshot: FeatureSnapshot,
  fallbackName: string,
): RecipeSet {
  return {
    id: newId('set'),
    name: `${snapshot.recipeSetName || fallbackName}（副本）`,
    universe: toGraphSymbols(snapshot.config.universe),
    frequency: snapshot.config.frequency,
    dateRange: [...snapshot.config.dateRange] as [string, string],
    recipes: snapshot.config.recipes.map(r => ({
      ...r,
      id: newId('recipe'),
      params: { ...r.params },
      selectedOutputs: [...r.selectedOutputs],
    })),
    createdAt: new Date().toISOString(),
    updatedAt: new Date().toISOString(),
  }
}

export { RESERVED_NAMES }
