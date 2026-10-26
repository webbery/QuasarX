<template>
  <div class="history">
    <div class="h-runs">
      <div class="h-run" v-for="run in runs" :key="run.id"
           :class="{ on: selectedIds.includes(run.id) }">
        <input type="checkbox" :checked="selectedIds.includes(run.id)" @change="toggle(run.id)" />
        <div class="h-run-main" @click="toggle(run.id)">
          <span class="h-run-time">{{ shortTime(run.createdAt) }}</span>
          <span class="h-run-name">{{ run.recipeSetName }}</span>
          <span class="h-run-tier">A{{ run.summary.n_tier_a }} / B{{ run.summary.n_tier_b }}</span>
        </div>
        <div class="h-run-actions">
          <button title="载入配方集" @click="$emit('load', run)">载入</button>
          <button title="按快照重新收集" @click="$emit('rerun', run)">重跑</button>
          <button title="查看完整明细" @click="$emit('detail', run)">明细</button>
          <button class="del" title="删除" @click="$emit('delete', run)">✕</button>
        </div>
      </div>
      <div v-if="!runs.length" class="h-empty">
        还没有历史运行。跑一次门禁后，结论会自动记录在这里。
      </div>
    </div>

    <div v-if="selectedRuns.length" class="h-toolbar">
      <span class="h-tip">已选 {{ selectedRuns.length }} / 4</span>
      <span class="h-tip">指标</span>
      <select v-model="metric" class="h-select">
        <option value="tierIc">档位 + DEV IC</option>
        <option value="dataCoverage">数据覆盖（标的）</option>
        <option value="coverage">有效交易日</option>
        <option value="nwT">NW t</option>
        <option value="q">q 值</option>
      </select>
      <button class="h-clear" @click="selectedIds = []">清空选择</button>
    </div>

    <div v-if="selectedRuns.length" class="h-table-wrap">
      <table class="h-table">
        <thead>
          <tr>
            <th class="h-feat">特征</th>
            <th v-for="run in selectedRuns" :key="run.id">
              <div class="h-col-head">
                <span class="h-col-time">{{ shortTime(run.createdAt) }}</span>
                <span class="h-col-meta">{{ colMeta(run) }}</span>
              </div>
            </th>
          </tr>
        </thead>
        <tbody>
          <tr v-for="row in rows" :key="row.name">
            <td class="h-feat" :title="row.name">{{ row.name }}</td>
            <td v-for="run in selectedRuns" :key="run.id"
                :class="cellClass(run, row.name)">
              {{ cellText(run, row.name) }}
              <span v-if="delta(run, row.name)" class="h-delta" :class="delta(run, row.name)!.dir">
                {{ delta(run, row.name)!.dir === 'up' ? '▲' : '▼' }}
              </span>
            </td>
          </tr>
        </tbody>
      </table>
    </div>
  </div>
</template>

<script setup lang="ts">
import { computed, ref } from 'vue'
import type { FeatureSnapshot, GateRun } from './types'

const props = defineProps<{
  runs: GateRun[]
  snapshots: FeatureSnapshot[]
}>()

const emit = defineEmits<{
  (e: 'load', run: GateRun): void
  (e: 'rerun', run: GateRun): void
  (e: 'detail', run: GateRun): void
  (e: 'delete', run: GateRun): void
}>()

const selectedIds = ref<string[]>([])
const metric = ref<'tierIc' | 'dataCoverage' | 'coverage' | 'nwT' | 'q'>('tierIc')

const selectedRuns = computed(() =>
  props.runs.filter(r => selectedIds.value.includes(r.id)).slice(0, 4),
)

/** 相对最旧一次运行的档位变化 */
const baselineRun = computed(() => selectedRuns.value[selectedRuns.value.length - 1])

const rows = computed(() => {
  const names = new Set<string>()
  for (const run of selectedRuns.value) {
    for (const n of run.analyzedFeatures) names.add(n)
  }
  const list = [...names].map(name => {
    const newest = selectedRuns.value[0]?.features[name]
    return { name, newest }
  })
  list.sort((a, b) => {
    const ta = a.newest?.tier || 'Z'
    const tb = b.newest?.tier || 'Z'
    if (ta !== tb) return ta.localeCompare(tb)
    return Math.abs(b.newest?.devIc || 0) - Math.abs(a.newest?.devIc || 0)
  })
  return list
})

function toggle(id: string) {
  const has = selectedIds.value.includes(id)
  if (has) {
    selectedIds.value = selectedIds.value.filter(x => x !== id)
  } else {
    if (selectedIds.value.length >= 4) return
    selectedIds.value = [...selectedIds.value, id]
  }
}

function snapshotOf(run: GateRun): FeatureSnapshot | undefined {
  return props.snapshots.find(s => s.id === run.featureSnapshotId)
}

function colMeta(run: GateRun): string {
  const snap = snapshotOf(run)
  const symbols = snap ? snap.config.universe.length : '?'
  return `${symbols}只 · N=${run.gateConfig.labelPeriod} · ${run.gateConfig.nFolds}折 · gap ${Math.max(run.gateConfig.foldGap, run.gateConfig.labelPeriod)}`
}

function shortTime(iso: string): string {
  if (!iso) return '—'
  const d = new Date(iso)
  if (Number.isNaN(d.getTime())) return iso
  const p = (n: number) => String(n).padStart(2, '0')
  return `${p(d.getMonth() + 1)}-${p(d.getDate())} ${p(d.getHours())}:${p(d.getMinutes())}`
}

function cellText(run: GateRun, name: string): string {
  const f = run.features[name]
  if (!f) return '—'
  if (metric.value === 'tierIc') return `${f.tier} ${fmt(f.devIc, 4)}`
  if (metric.value === 'coverage') return `${fmt(f.coverage * 100, 0)}%`
  if (metric.value === 'nwT') return fmt(f.nwT, 2)
  if (metric.value === 'q') return f.q == null ? '—' : fmt(f.q, 3)
  const snap = snapshotOf(run)
  const s = snap?.features[name]
  return s ? `${fmt(s.dataCoverage * 100, 0)}%` : '—'
}

function cellClass(run: GateRun, name: string): string {
  const f = run.features[name]
  if (!f) return 'miss'
  if (metric.value === 'tierIc') return f.tier === 'A' ? 'tier-a' : f.tier === 'B' ? 'tier-b' : ''
  return ''
}

function delta(run: GateRun, name: string): { dir: 'up' | 'down'; text: string } | null {
  if (metric.value !== 'tierIc') return null
  if (run.id === baselineRun.value?.id) return null
  const cur = run.features[name]?.tier
  const base = baselineRun.value?.features[name]?.tier
  if (!cur || !base || cur === base) return null
  return cur < base ? { dir: 'up', text: base } : { dir: 'down', text: base }
}

function fmt(v: any, nd: number): string {
  return typeof v === 'number' && Number.isFinite(v) ? v.toFixed(nd) : '—'
}

function clear() {
  selectedIds.value = []
}

defineExpose({ clear })
</script>

<style scoped>
.history { padding: 12px 14px 24px; }

.h-runs { display: flex; flex-direction: column; gap: 4px; }

.h-run {
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 5px 8px;
  background: rgba(15, 25, 41, 0.5);
  border: 1px solid rgba(74, 85, 104, 0.22);
  border-radius: 3px;
}
.h-run.on { border-color: rgba(56, 189, 248, 0.45); background: rgba(56, 189, 248, 0.06); }
.h-run input { accent-color: #3b82f6; }

.h-run-main { display: flex; align-items: baseline; gap: 10px; flex: 1; min-width: 0; cursor: pointer; }
.h-run-time { font-family: 'SF Mono', Consolas, monospace; font-size: 10.5px; color: #94a3b8; }
.h-run-name { font-size: 11.5px; color: #e2e8f0; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
.h-run-tier { font-family: 'SF Mono', Consolas, monospace; font-size: 10px; color: #6b7f94; }

.h-run-actions { display: flex; gap: 4px; flex: 0 0 auto; }
.h-run-actions button {
  padding: 1px 6px;
  background: transparent;
  border: 1px solid rgba(74, 85, 104, 0.35);
  border-radius: 2px;
  color: #93c5fd;
  font-family: inherit;
  font-size: 9.5px;
  cursor: pointer;
}
.h-run-actions button:hover { background: rgba(56, 189, 248, 0.12); }
.h-run-actions button.del { color: #6b7f94; }
.h-run-actions button.del:hover { color: #f87171; border-color: rgba(248, 113, 113, 0.4); }

.h-empty { padding: 28px 12px; text-align: center; font-size: 11.5px; color: #4f5d6d; }

.h-toolbar { display: flex; align-items: center; gap: 8px; margin: 12px 0 8px; }
.h-tip { font-size: 10px; color: #7d8c9e; }
.h-select {
  padding: 3px 6px;
  background: #0f161e;
  border: 1px solid rgba(74, 85, 104, 0.4);
  border-radius: 3px;
  color: #dbe4ee;
  font-family: inherit;
  font-size: 10.5px;
  outline: none;
}
.h-clear {
  margin-left: auto;
  background: none;
  border: none;
  color: #5b8ff9;
  font-family: inherit;
  font-size: 10px;
  cursor: pointer;
}

.h-table-wrap { overflow-x: auto; border: 1px solid rgba(74, 85, 104, 0.22); border-radius: 4px; }

.h-table { width: 100%; border-collapse: collapse; font-size: 11px; }
.h-table th {
  position: sticky;
  top: 0;
  background: rgba(19, 28, 46, 0.97);
  color: #94a3b8;
  font-weight: 600;
  text-align: left;
  padding: 6px 8px;
  border-bottom: 1px solid rgba(74, 85, 104, 0.35);
}
.h-table td {
  padding: 5px 8px;
  border-bottom: 1px solid rgba(74, 85, 104, 0.14);
  color: #cbd5e1;
  font-family: 'SF Mono', Consolas, monospace;
  white-space: nowrap;
}
.h-feat { max-width: 220px; overflow: hidden; text-overflow: ellipsis; color: #e2e8f0; }

.h-col-head { display: flex; flex-direction: column; gap: 1px; }
.h-col-time { font-size: 10.5px; color: #cbd5e1; }
.h-col-meta { font-size: 9px; color: #64748b; font-weight: 400; }

.h-table td.miss { color: #4f5d6d; }
.h-table td.tier-a { color: #34d399; }
.h-table td.tier-b { color: #fbbf24; }

.h-delta { font-size: 8px; margin-left: 3px; }
.h-delta.up { color: #34d399; }
.h-delta.down { color: #f87171; }
</style>
