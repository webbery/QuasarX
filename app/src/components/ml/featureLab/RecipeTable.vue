<template>
  <div class="recipes">
    <div class="r-head">
      <span class="r-title">配方</span>
      <span class="r-count">{{ modelValue.length }} 节点 · {{ endpointCount }} 端点</span>
    </div>

    <div v-for="(r, idx) in modelValue" :key="r.id" class="r-card">
      <div class="r-row">
        <span class="r-type" :class="r.nodeType">{{ typeLabel(r) }}</span>
        <input class="r-name" :value="r.name" @input="rename(idx, ($event.target as HTMLInputElement).value)"
               :class="{ bad: nameError(idx) }" spellcheck="false" />
        <span v-if="nameError(idx)" class="r-err">{{ nameError(idx) }}</span>
        <button class="r-del" title="删除" @click="remove(idx)">✕</button>
      </div>

      <div v-if="r.nodeType === 'function'" class="r-params">
        <select :value="r.params.method" class="r-input"
                @change="patchParams(idx, { method: ($event.target as HTMLSelectElement).value })">
          <option v-for="m in FUNCTION_METHOD_OPTIONS" :key="m" :value="m">{{ m }}</option>
        </select>
        <span class="r-k">窗口</span>
        <input type="number" min="1" max="1000" :value="r.params.range" class="r-input narrow"
               @input="patchParams(idx, { range: Number(($event.target as HTMLInputElement).value) })" />
      </div>

      <div v-else-if="r.nodeType === 'formula'" class="r-params col">
        <textarea :value="r.params.expression" rows="2" class="r-input mono"
                  placeholder="如 ma20[t] + 2*std20[t]"
                  @input="patchParams(idx, { expression: ($event.target as HTMLTextAreaElement).value })"></textarea>
        <span class="r-avail">可用：{{ availableVars(idx).join(' / ') || '（无）' }}</span>
      </div>

      <div v-else class="r-params col">
        <div class="r-params">
          <select :value="r.params.method" class="r-input"
                  @change="patchParams(idx, { method: ($event.target as HTMLSelectElement).value })">
            <option v-for="m in EMD_METHOD_OPTIONS" :key="m" :value="m">{{ m.toUpperCase() }}</option>
          </select>
          <span class="r-k">IMF</span>
          <input type="number" min="1" max="20" :value="r.params.numIMFs" class="r-input narrow"
                 @input="patchParams(idx, { numIMFs: Number(($event.target as HTMLInputElement).value) })" />
          <span class="r-k">窗口</span>
          <input type="number" min="0" max="500" :value="r.params.windowSize" class="r-input narrow"
                 @input="patchParams(idx, { windowSize: Number(($event.target as HTMLInputElement).value) })" />
        </div>
        <div class="r-params">
          <span class="r-k">分解</span>
          <select :value="r.params.inputField" class="r-input"
                  @change="onEmdField(idx, ($event.target as HTMLSelectElement).value)">
            <option v-for="f in EMD_INPUT_FIELDS" :key="f" :value="f">{{ f }}</option>
          </select>
          <label class="r-check">
            <input type="checkbox" :checked="r.params.computeEnergyVelocity"
                   @change="patchParams(idx, { computeEnergyVelocity: ($event.target as HTMLInputElement).checked })" />
            energy_velocity
          </label>
          <label class="r-check" :class="{ disabled: !canComputeVolumeRegime(r) }">
            <input type="checkbox" :checked="r.params.computeVolumeRegime"
                   :disabled="!canComputeVolumeRegime(r)"
                   @change="patchParams(idx, { computeVolumeRegime: ($event.target as HTMLInputElement).checked })" />
            volume_regime
          </label>
        </div>
        <div v-if="r.params.method === 'ceemdan'" class="r-params">
          <span class="r-k">集合数</span>
          <input type="number" min="10" max="200" :value="r.params.ensembles" class="r-input narrow"
                 @input="patchParams(idx, { ensembles: Number(($event.target as HTMLInputElement).value) })" />
          <span class="r-k">噪声</span>
          <input type="number" min="0.01" max="1" step="0.01" :value="r.params.noiseStd" class="r-input narrow"
                 @input="patchParams(idx, { noiseStd: Number(($event.target as HTMLInputElement).value) })" />
        </div>
        <div v-if="r.params.method === 'vmd'" class="r-params">
          <span class="r-k">α</span>
          <input type="number" min="100" max="10000" :value="r.params.alpha" class="r-input narrow"
                 @input="patchParams(idx, { alpha: Number(($event.target as HTMLInputElement).value) })" />
          <span class="r-k">τ</span>
          <input type="number" min="0" max="1" step="0.1" :value="r.params.tau" class="r-input narrow"
                 @input="patchParams(idx, { tau: Number(($event.target as HTMLInputElement).value) })" />
          <span class="r-k">tol</span>
          <input type="number" :value="r.params.tol" class="r-input narrow"
                 @input="patchParams(idx, { tol: Number(($event.target as HTMLInputElement).value) })" />
        </div>
      </div>

      <div class="r-eps">
        <label v-for="ep in resolveOutputs(r)" :key="ep.name" class="r-ep"
               :class="{ on: r.selectedOutputs.includes(ep.name), invalid: invalidOutput(r, ep.name) }">
          <input type="checkbox" :checked="r.selectedOutputs.includes(ep.name)"
                 @change="toggleOutput(idx, ep.name)" />
          <span>{{ ep.label }}</span>
          <span v-if="!ep.formulaRef" class="r-ep-tag" title="多段名，不能用作公式变量">不可公式</span>
        </label>
        <span v-for="lost in lostOutputs(r)" :key="lost" class="r-ep invalid on">
          <span>{{ lost }}（不存在）</span>
          <button class="r-ep-x" @click="dropOutput(idx, lost)">✕</button>
        </span>
      </div>

      <div v-if="recipeError(idx)" class="r-recipe-err">{{ recipeError(idx) }}</div>
    </div>

    <div class="r-add">
      <button class="r-add-btn" @click="add('function')">+ 指标</button>
      <button class="r-add-btn" @click="add('formula')">+ 公式</button>
      <button class="r-add-btn" @click="add('emd')">+ EMD</button>
    </div>
  </div>
</template>

<script setup lang="ts">
import { computed } from 'vue'
import type { Recipe, RecipeNodeType } from './types'
import {
  FUNCTION_METHOD_OPTIONS,
  EMD_METHOD_OPTIONS,
  EMD_INPUT_FIELDS,
  RAW_FIELDS,
  resolveOutputs,
  canComputeVolumeRegime,
  createRecipe,
  validateRecipeName,
} from './outputSpecs'
import { extractFormulaIdentifiers } from './useRecipeGraph'

const props = defineProps<{ modelValue: Recipe[] }>()
const emit = defineEmits<{ (e: 'update:modelValue', v: Recipe[]): void }>()

const endpointCount = computed(() =>
  props.modelValue.reduce((n, r) => n + resolveOutputs(r).length, 0),
)

function emitAll(next: Recipe[]) {
  emit('update:modelValue', next)
}

function patch(idx: number, patch: Partial<Recipe>) {
  const next = props.modelValue.map((r, i) => (i === idx ? { ...r, ...patch } : r))
  emitAll(next)
}

/** 改名时同步重映射已勾选端点（端点名里带节点名） */
function rename(idx: number, newName: string) {
  const next = props.modelValue.map((r, i) => {
    if (i !== idx) return r
    const old = r.name
    const remap = (ep: string) => {
      if (ep === old) return newName
      if (ep.startsWith(`${old}.`)) return newName + ep.slice(old.length)
      return ep
    }
    return { ...r, name: newName, selectedOutputs: r.selectedOutputs.map(remap) }
  })
  emitAll(next)
}

function patchParams(idx: number, patch: Record<string, any>) {
  const next = props.modelValue.map((r, i) => {
    if (i !== idx) return r
    const params = { ...r.params, ...patch }
    // volume_regime 只在分解 volume 时可用
    if (params.inputField !== 'volume') params.computeVolumeRegime = false
    return { ...r, params }
  })
  emitAll(next)
}

function onEmdField(idx: number, field: string) {
  patchParams(idx, { inputField: field })
}

function add(nodeType: RecipeNodeType) {
  const recipe = createRecipe(nodeType, props.modelValue)
  emitAll([...props.modelValue, recipe])
}

function remove(idx: number) {
  emitAll(props.modelValue.filter((_, i) => i !== idx))
}

function toggleOutput(idx: number, name: string) {
  const next = props.modelValue.map((r, i) => {
    if (i !== idx) return r
    const has = r.selectedOutputs.includes(name)
    return {
      ...r,
      selectedOutputs: has
        ? r.selectedOutputs.filter(x => x !== name)
        : [...r.selectedOutputs, name],
    }
  })
  emitAll(next)
}

function dropOutput(idx: number, name: string) {
  const next = props.modelValue.map((r, i) =>
    i === idx ? { ...r, selectedOutputs: r.selectedOutputs.filter(x => x !== name) } : r,
  )
  emitAll(next)
}

function lostOutputs(r: Recipe): string[] {
  const valid = new Set(resolveOutputs(r).map(o => o.name))
  return r.selectedOutputs.filter(x => !valid.has(x))
}

function invalidOutput(r: Recipe, name: string): boolean {
  return !resolveOutputs(r).some(o => o.name === name)
}

function typeLabel(r: Recipe): string {
  if (r.nodeType === 'function') return '指标'
  if (r.nodeType === 'formula') return '公式'
  return 'EMD'
}

function nameError(idx: number): string {
  const r = props.modelValue[idx]
  const others = props.modelValue.filter((_, i) => i !== idx).map(x => x.name)
  return validateRecipeName(r.name, others) || ''
}

function availableVars(idx: number): string[] {
  const prior = props.modelValue.slice(0, idx)
  return [...RAW_FIELDS, ...prior.filter(r => r.nodeType !== 'emd').map(r => r.name)]
}

function recipeError(idx: number): string {
  const r = props.modelValue[idx]
  if (r.nodeType !== 'formula') return ''
  const expr = String(r.params.expression || '').trim()
  if (!expr) return ''
  const allowed = new Set(availableVars(idx))
  const bad = extractFormulaIdentifiers(expr).filter(id => !allowed.has(id))
  return bad.length ? `未定义变量：${bad.join(', ')}` : ''
}
</script>

<style scoped>
.recipes { padding: 10px 12px 8px; }

.r-head { display: flex; align-items: baseline; gap: 8px; margin-bottom: 7px; }
.r-title { font-size: 11px; font-weight: 600; color: #b9c7d5; letter-spacing: 0.5px; }
.r-count { font-size: 9.5px; color: #6b7f94; font-family: 'SF Mono', Consolas, monospace; }

.r-card {
  margin-bottom: 8px;
  padding: 7px 8px;
  background: rgba(10, 18, 32, 0.55);
  border: 1px solid rgba(74, 85, 104, 0.25);
  border-radius: 4px;
}

.r-row { display: flex; align-items: center; gap: 6px; }

.r-type {
  flex: 0 0 auto;
  padding: 1px 5px;
  border-radius: 2px;
  font-size: 9px;
  background: rgba(91, 143, 249, 0.15);
  color: #93c5fd;
}
.r-type.formula { background: rgba(167, 139, 250, 0.15); color: #c4b5fd; }
.r-type.emd { background: rgba(52, 211, 153, 0.13); color: #6ee7b7; }

.r-name {
  flex: 1;
  min-width: 0;
  padding: 2px 5px;
  background: #0f161e;
  border: 1px solid transparent;
  border-radius: 2px;
  color: #e2e8f0;
  font-family: 'SF Mono', Consolas, monospace;
  font-size: 11px;
  outline: none;
}
.r-name:focus { border-color: #38bdf8; }
.r-name.bad { border-color: rgba(248, 113, 113, 0.6); }

.r-err { font-size: 9px; color: #f87171; white-space: nowrap; }

.r-del {
  flex: 0 0 auto;
  background: none;
  border: none;
  color: #4f5d6d;
  font-size: 10px;
  cursor: pointer;
  padding: 0 2px;
}
.r-del:hover { color: #f87171; }

.r-params { display: flex; align-items: center; gap: 5px; margin-top: 6px; flex-wrap: wrap; }
.r-params.col { flex-direction: column; align-items: stretch; gap: 4px; }

.r-k { font-size: 9.5px; color: #6b7f94; }

.r-input {
  padding: 2px 5px;
  background: #0f161e;
  border: 1px solid rgba(74, 85, 104, 0.4);
  border-radius: 2px;
  color: #dbe4ee;
  font-family: inherit;
  font-size: 10.5px;
  outline: none;
}
.r-input:focus { border-color: #38bdf8; }
.r-input.narrow { width: 52px; }
.r-input.mono { width: 100%; box-sizing: border-box; font-family: 'SF Mono', Consolas, monospace; resize: vertical; }

.r-avail { font-size: 9px; color: #4f5d6d; font-family: 'SF Mono', Consolas, monospace; word-break: break-all; }

.r-check { display: inline-flex; align-items: center; gap: 4px; font-size: 9.5px; color: #94a3b8; cursor: pointer; }
.r-check.disabled { opacity: 0.45; cursor: not-allowed; }
.r-check input { accent-color: #3b82f6; }

.r-eps { display: flex; flex-wrap: wrap; gap: 4px; margin-top: 7px; }

.r-ep {
  display: inline-flex;
  align-items: center;
  gap: 3px;
  padding: 1px 5px;
  border-radius: 2px;
  background: rgba(74, 85, 104, 0.12);
  color: #7d8c9e;
  font-size: 9.5px;
  font-family: 'SF Mono', Consolas, monospace;
  cursor: pointer;
}
.r-ep.on { background: rgba(56, 189, 248, 0.14); color: #a5e0ff; }
.r-ep.invalid { background: rgba(248, 113, 113, 0.12); color: #f87171; }
.r-ep input { accent-color: #3b82f6; width: 10px; height: 10px; }

.r-ep-tag { font-size: 8px; color: #4f5d6d; }
.r-ep-x { background: none; border: none; color: inherit; cursor: pointer; padding: 0; font-size: 9px; }

.r-recipe-err { margin-top: 5px; font-size: 9.5px; color: #f87171; }

.r-add { display: flex; gap: 6px; }
.r-add-btn {
  flex: 1;
  padding: 4px 0;
  background: rgba(91, 143, 249, 0.07);
  border: 1px dashed rgba(74, 85, 104, 0.45);
  border-radius: 3px;
  color: #93c5fd;
  font-family: inherit;
  font-size: 10.5px;
  cursor: pointer;
}
.r-add-btn:hover { background: rgba(91, 143, 249, 0.14); }
</style>
