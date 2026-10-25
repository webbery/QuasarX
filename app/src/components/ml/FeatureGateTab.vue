<template>
  <div class="gate-tab">
    <RecipeSetBar
      :sets="sets"
      :current="current"
      :dirty="dirty"
      :collecting="collecting"
      :issue-count="issues.length"
      :issue-text="issueText"
      @select="onSelectSet"
      @patch="patchSet"
      @save="onSave"
      @save-as="onSaveAs"
      @new="onNew"
      @collect="onCollect"
    />

    <div class="gate-body" :class="{ 'inspector-collapsed': inspectorCollapsed }">
      <main class="gate-main">
        <!-- 收集进度 -->
        <div v-if="collectSteps.length && (collecting || !lastCollect)" class="collect-progress">
          <div class="cp-head">
            <span class="cp-title">收集进度</span>
            <span v-if="!collecting" class="cp-done">完成</span>
          </div>
          <div class="cp-list">
            <div v-for="step in collectSteps" :key="step.id" class="cp-item">
              <span class="cp-icon" :class="step.status">
                <template v-if="step.status === 'done'">✓</template>
                <template v-else-if="step.status === 'running'">⏳</template>
                <template v-else-if="step.status === 'error'">✗</template>
                <template v-else>○</template>
              </span>
              <span class="cp-label">{{ step.label }}</span>
              <span v-if="step.detail" class="cp-detail">{{ step.detail }}</span>
            </div>
          </div>
        </div>

        <!-- 收集快照摘要 -->
        <div v-if="lastCollect" class="snap-bar">
          <span class="snap-tag">特征缓存</span>
          <span class="snap-text">
            {{ lastCollect.snapshot.config.universe.length }} 标的 ·
            {{ lastCollect.snapshot.collectedOutputs.length }} 端点 ·
            {{ shortDate(lastCollect.snapshot.dateStart) }} ~ {{ shortDate(lastCollect.snapshot.dateEnd) }} ·
            {{ lastCollect.snapshot.totalRows }} 行
          </span>
          <span v-if="uncollectedCount" class="snap-warn" :title="uncollectedText">
            {{ uncollectedCount }} 个端点未收集
          </span>
          <span v-else-if="staleSource" class="snap-warn" title="配方节点/标的池/频率/区间已修改，当前结论基于旧特征缓存">
            特征源已修改
          </span>
        </div>

        <!-- 视图切换 -->
        <div class="main-head">
          <button class="mh-tab" :class="{ active: view === 'result' }" @click="view = 'result'">本次结论</button>
          <button class="mh-tab" :class="{ active: view === 'history' }" @click="view = 'history'">
            历史对比 <span class="mh-count">{{ runs.length }}</span>
          </button>
        </div>

        <div v-show="view === 'result'">
          <FeatureGatePanel
            v-if="hasResult || csvPath"
            :state="state"
            :csv-path="historical ? '' : csvPath"
            :features="historical ? undefined : selectedEndpoints"
            :historical="historical"
            @finished="onGateFinished"
            @exit-history="exitHistory"
          />
          <div v-else-if="!collecting" class="stage-empty">
            <div class="stage-icon">🚦</div>
            <div>
              <h3>配一个特征集，然后收集</h3>
              <p>右侧选择标的池、添加指标/公式/EMD 配方，勾选要分析的输出端点，再点「开始收集」。收集完成后即可门禁。</p>
            </div>
          </div>
        </div>

        <RunHistory
          v-show="view === 'history'"
          :runs="runs"
          :snapshots="snapshots"
          @load="onLoadRun"
          @rerun="onRerun"
          @detail="onDetail"
          @delete="onDeleteRun"
        />
      </main>

      <aside class="gate-inspector">
        <button class="inspector-toggle" :title="inspectorCollapsed ? '展开' : '折叠'"
                @click="inspectorCollapsed = !inspectorCollapsed">
          {{ inspectorCollapsed ? '‹' : '›' }}
        </button>
        <div v-show="!inspectorCollapsed" class="inspector-body">
          <UniversePanel
            :model-value="current.universe"
            :min-names="state.screenConfig.minNames"
            @update:model-value="v => patchSet({ universe: v })"
          />
          <div class="insp-sep"></div>
          <RecipeTable
            :model-value="current.recipes"
            @update:model-value="v => patchSet({ recipes: v })"
          />
          <div class="insp-sep"></div>
          <GateConfigForm :state="state" />
        </div>
      </aside>
    </div>
  </div>
</template>

<script setup lang="ts">
import { computed, nextTick, onMounted, ref } from 'vue'
import { ElMessage, ElMessageBox } from 'element-plus'
import { useMLState } from './composables/useMLState'
import { useMLData } from './composables/useMLData'
import type { FeatureReport, ScreenReport, ScreenTaskResult } from './composables/useMLState'
import { toGraphSymbols } from '@/lib/symbol'
import type { FeatureSnapshot, GateRun, RecipeSet, ScreenConfig } from './featureLab/types'
import { newId, todayISO } from './featureLab/types'
import { buildRecipeGraph, validateRecipeSet, selectedFeatures, aggregateReport, recipeSetFromSnapshot } from './featureLab/useRecipeGraph'
import { useRecipeStorage } from './featureLab/useRecipeStorage'
import RecipeSetBar from './featureLab/RecipeSetBar.vue'
import UniversePanel from './featureLab/UniversePanel.vue'
import RecipeTable from './featureLab/RecipeTable.vue'
import GateConfigForm from './featureLab/GateConfigForm.vue'
import RunHistory from './featureLab/RunHistory.vue'
import FeatureGatePanel from './panels/FeatureGatePanel.vue'

const COLLECT_STEP_LABELS: Record<string, string> = {
  parse_script: '解析合成图',
  init_nodes: '初始化节点',
  start_exchange: '启动数据源',
  collect_data: '收集特征数据',
  analyze: '分析特征数据',
}

const state = useMLState()
const { collect, screenResult } = useMLData()
const storage = useRecipeStorage()
const { sets, snapshots, runs } = storage

const current = ref<RecipeSet>(newDraft())
const dirty = ref(false)
const view = ref<'result' | 'history'>('result')
const inspectorCollapsed = ref(false)
const collecting = ref(false)
const lastCollect = ref<{ snapshot: FeatureSnapshot; csvPath: string } | null>(null)
const historical = ref(false)
let liveScreen: { task: ScreenTaskResult | null; report: ScreenReport | null } = {
  task: null,
  report: null,
}

function newDraft(): RecipeSet {
  const end = todayISO()
  const d = new Date()
  d.setFullYear(d.getFullYear() - 3)
  const start = d.toISOString().slice(0, 10)
  return {
    id: newId('set'),
    name: `配方集 ${new Date().toLocaleDateString()}`,
    universe: [],
    frequency: '1d',
    dateRange: [start, end],
    recipes: [],
    createdAt: new Date().toISOString(),
    updatedAt: new Date().toISOString(),
  }
}

const issues = computed(() => validateRecipeSet(current.value, state.screenConfig.minNames))
const issueText = computed(() => issues.value.map(i => i.message).join('\n'))
const allSelectedEndpoints = computed(() => selectedFeatures(current.value))
const collectSteps = computed(() => state.featureReport.steps)
const csvPath = computed(() =>
  historical.value || collecting.value ? '' : lastCollect.value?.csvPath || '',
)
const hasResult = computed(() => !!state.screenState.task)

/** 只把实际收集到的端点送进门禁，避免旧缓存里缺少新勾选的端点 */
const selectedEndpoints = computed(() => {
  const snap = lastCollect.value?.snapshot
  if (!snap) return allSelectedEndpoints.value
  return allSelectedEndpoints.value.filter(f => snap.collectedOutputs.includes(f))
})

const uncollectedEndpoints = computed(() => {
  const snap = lastCollect.value?.snapshot
  if (!snap) return []
  return allSelectedEndpoints.value.filter(f => !snap.collectedOutputs.includes(f))
})
const uncollectedCount = computed(() => uncollectedEndpoints.value.length)
const uncollectedText = computed(
  () => `这些端点还没收集，需要重新收集：${uncollectedEndpoints.value.join(', ')}`,
)

/** 收集后特征源是否被改过；端点勾选变化不算（只影响门禁白名单） */
const staleSource = computed(() => {
  const snap = lastCollect.value?.snapshot
  if (!snap) return false
  return sourceSignature(snap.config) !== sourceSignature(current.value)
})

function sourceSignature(set: RecipeSet | FeatureSnapshot['config']): string {
  return JSON.stringify({
    universe: set.universe,
    frequency: set.frequency,
    dateRange: set.dateRange,
    recipes: set.recipes.map(r => ({ name: r.name, nodeType: r.nodeType, params: r.params })),
  })
}

function shortDate(s: string): string {
  return typeof s === 'string' ? s.split(' ')[0] : String(s ?? '')
}

function patchSet(p: Partial<RecipeSet>) {
  current.value = { ...current.value, ...p }
  dirty.value = true
}

// ── 配方集 ────────────────────────────────────────────────
async function loadSet(id: string) {
  const found = sets.value.find(s => s.id === id)
  if (!found) return
  current.value = JSON.parse(JSON.stringify(found)) as RecipeSet
  // 兼容旧配方集里的裸代码
  current.value.universe = toGraphSymbols(current.value.universe)
  dirty.value = false
  lastCollect.value = null
  historical.value = false
  state.reset()
}

async function onSelectSet(id: string) {
  if (id === current.value.id) return
  if (!(await confirmDiscard())) return
  loadSet(id)
}

async function confirmDiscard(): Promise<boolean> {
  if (!dirty.value) return true
  try {
    await ElMessageBox.confirm('当前配方集有未保存的修改，确定丢弃？', '未保存', {
      confirmButtonText: '丢弃', cancelButtonText: '取消', type: 'warning',
    })
    return true
  } catch {
    return false
  }
}

async function onSave() {
  if (!current.value.name.trim()) {
    ElMessage.warning('配方集名称不能为空')
    return
  }
  await storage.saveSet(current.value)
  dirty.value = false
  ElMessage.success('已保存')
}

async function onSaveAs() {
  try {
    const { value } = await ElMessageBox.prompt('新的配方集名称', '另存为', {
      inputValue: `${current.value.name} 副本`,
      confirmButtonText: '保存', cancelButtonText: '取消',
    })
    if (!value?.trim()) return
    const copy: RecipeSet = JSON.parse(JSON.stringify(current.value))
    copy.id = newId('set')
    copy.name = value.trim()
    copy.createdAt = new Date().toISOString()
    await storage.saveSet(copy)
    current.value = copy
    dirty.value = false
    ElMessage.success('已另存')
  } catch { /* cancelled */ }
}

async function onNew() {
  if (!(await confirmDiscard())) return
  current.value = newDraft()
  dirty.value = false
  lastCollect.value = null
  historical.value = false
  state.reset()
}

// ── 收集 ──────────────────────────────────────────────────
async function onCollect() {
  if (collecting.value) return
  if (issues.value.length) {
    ElMessage.warning(issues.value[0].message)
    return
  }

  const fr = state.featureReport
  fr.loading = true
  fr.data = null
  fr.steps = []
  fr.logs = []
  state.screenState.task = null
  state.screenState.report = null
  state.screenState.steps = []
  state.screenState.logs = []
  state.screenState.error = ''
  historical.value = false
  collecting.value = true

  try {
    const set = current.value
    const script = buildRecipeGraph(set, set.frequency)
    // 排查用：确认发给服务端的标的代码是否带交易所前缀
    try {
      const parsed = JSON.parse(script)
      console.info('[FeatureLab] collect input codes', parsed?.nodes?.[0]?.data?.params?.code?.value)
    } catch { /* ignore */ }
    const report = await collect(
      script,
      {
        labelSource: '',
        startDate: set.dateRange[0],
        endDate: set.dateRange[1],
        frequency: set.frequency,
      },
      (type: string, data: any) => {
        if (type === 'step') {
          const existing = fr.steps.find((s: any) => s.id === data.step)
          if (data.status === 'start') {
            if (!existing) {
              fr.steps.push({
                id: data.step,
                label: COLLECT_STEP_LABELS[data.step] || data.step,
                status: 'running',
                detail: data.msg,
              })
            }
          } else if (data.status === 'done' && existing) {
            existing.status = 'done'
          } else if (data.status === 'error' && existing) {
            existing.status = 'error'
          }
        } else if (type === 'progress') {
          const step = fr.steps.find((s: any) => s.id === data.step)
          if (step) step.detail = `${data.current} / ${data.total}`
        }
      },
    )

    if (!report?.csv_path) {
      ElMessage.error('收集没有返回特征缓存')
      return
    }

    fr.data = report
    const agg = aggregateReport(report as FeatureReport, set.recipes, set.universe)
    if (!agg.collectedOutputs.length) {
      ElMessage.error('没有收集到任何端点列，请检查配方参数')
      return
    }

    const snapshot: FeatureSnapshot = {
      id: newId('snap'),
      createdAt: new Date().toISOString(),
      recipeSetId: set.id || null,
      recipeSetName: set.name,
      config: JSON.parse(JSON.stringify({
        universe: set.universe,
        frequency: set.frequency,
        dateRange: set.dateRange,
        recipes: set.recipes,
      })),
      totalRows: report.total_rows,
      nColumns: report.features.length,
      dateStart: report.date_start,
      dateEnd: report.date_end,
      collectedOutputs: agg.collectedOutputs,
      features: agg.features,
    }
    lastCollect.value = { snapshot, csvPath: report.csv_path }
    await storage.addSnapshot(snapshot)
    ElMessage.success(`收集完成：${agg.collectedOutputs.length} 个端点`)
  } catch (e: any) {
    ElMessage.error(`收集处理失败: ${e?.message || e}`)
  } finally {
    fr.loading = false
    collecting.value = false
  }
}

// ── 门禁完成 → 落台账 ──────────────────────────────────────
function composeScreenConfig(): ScreenConfig {
  const cfg = state.config
  return {
    ...JSON.parse(JSON.stringify(state.screenConfig)),
    labelPeriod: cfg.labelPeriod,
    volK: cfg.volK,
    nFolds: cfg.nFolds,
    foldGap: cfg.foldGap,
    sigmaMode: cfg.sigmaMode,
  }
}

async function onGateFinished(task: ScreenTaskResult, report: ScreenReport | null) {
  const snap = lastCollect.value
  if (!snap) return
  if (!report) {
    ElMessage.warning('门禁汇总已返回，但明细获取失败，本次不写入历史')
    return
  }

  const compact: GateRun['features'] = {}
  for (const [name, f] of Object.entries(report.features)) {
    compact[name] = {
      tier: f.selection?.tier || 'C',
      sign: f.sign ?? 1,
      devIc: f.dev?.ic_mean ?? NaN,
      nwT: f.dev?.nw_t ?? NaN,
      q: f.q_value ?? null,
      coverage: f.dev?.coverage ?? 0,
      signFolds: f.dev?.folds?.sign_consistency ?? 0,
      testIc: f.test?.ic_mean ?? null,
    }
  }

  const run: GateRun = {
    id: newId('run'),
    createdAt: new Date().toISOString(),
    featureSnapshotId: snap.snapshot.id,
    recipeSetId: current.value.id || null,
    recipeSetName: current.value.name,
    serverRunId: task.run_id,
    analyzedFeatures: Object.keys(compact),
    gateConfig: composeScreenConfig(),
    summary: task.summary,
    features: compact,
  }
  try {
    await storage.addRun(run)
    view.value = 'result'
  } catch (e: any) {
    ElMessage.error(`历史记录写入失败: ${e?.message || e}`)
  }
}
// ── 历史运行 ──────────────────────────────────────────────
function snapshotOf(run: GateRun): FeatureSnapshot | undefined {
  return snapshots.value.find(s => s.id === run.featureSnapshotId)
}

async function onLoadRun(run: GateRun): Promise<boolean> {
  const snap = snapshotOf(run)
  if (!snap) {
    ElMessage.warning('这次运行的收集快照已被裁剪，无法载入')
    return false
  }
  if (!(await confirmDiscard())) return false
  current.value = recipeSetFromSnapshot(snap, run.recipeSetName)
  dirty.value = true
  lastCollect.value = null
  historical.value = false
  state.reset()
  ElMessage.info('已载入快照，请重新收集后再跑门禁')
  return true
}

async function onRerun(run: GateRun) {
  const ok = await onLoadRun(run)
  if (!ok) return
  await nextTick()
  if (!issues.value.length) await onCollect()
}

async function onDetail(run: GateRun) {
  try {
    const report = await screenResult(run.serverRunId)
    if (!report) {
      ElMessage.error('服务端已找不到这次运行的明细')
      return
    }
    liveScreen = { task: state.screenState.task, report: state.screenState.report }
    state.screenState.task = {
      run_id: run.serverRunId,
      out_dir: '',
      files: {},
      summary: run.summary,
      meta: {},
    } as any
    state.screenState.report = report
    historical.value = true
    view.value = 'result'
  } catch (e: any) {
    ElMessage.error(`明细加载失败: ${e?.message || e}`)
  }
}

function exitHistory() {
  state.screenState.task = liveScreen.task
  state.screenState.report = liveScreen.report
  historical.value = false
}

async function onDeleteRun(run: GateRun) {
  try {
    await ElMessageBox.confirm(
      `删除 ${new Date(run.createdAt).toLocaleString()} 的运行记录？`,
      '删除运行记录',
      { confirmButtonText: '删除', cancelButtonText: '取消', type: 'warning' },
    )
    await storage.deleteRun(run.id)
  } catch { /* cancelled */ }
}

onMounted(async () => {
  try {
    await storage.refresh()
    if (sets.value.length) {
      current.value = JSON.parse(JSON.stringify(sets.value[0])) as RecipeSet
      current.value.universe = toGraphSymbols(current.value.universe)
      dirty.value = false
    }
  } catch (e: any) {
    ElMessage.error(`本地配方库打开失败: ${e?.message || e}`)
  }
})
</script>

<style scoped>
.gate-tab {
  display: flex;
  flex-direction: column;
  height: 100%;
  background: #0a0e13;
  color: #dbe4ee;
}

.gate-body {
  flex: 1;
  min-height: 0;
  display: grid;
  grid-template-columns: minmax(0, 1fr) 360px;
}
.gate-body.inspector-collapsed { grid-template-columns: minmax(0, 1fr) 30px; }

.gate-main {
  min-width: 0;
  overflow: auto;
  padding: 10px 14px 20px;
}
.gate-main::-webkit-scrollbar { width: 6px; }
.gate-main::-webkit-scrollbar-thumb { background: #22303f; }

/* ── 收集进度 ─────────────────────────────── */
.collect-progress {
  margin-bottom: 10px;
  padding: 9px 12px;
  background: #0d141c;
  border: 1px solid rgba(74, 85, 104, 0.28);
  border-radius: 4px;
}
.cp-head { display: flex; justify-content: space-between; align-items: center; margin-bottom: 6px; }
.cp-title { font-size: 11px; color: #b9c7d5; font-weight: 600; }
.cp-done { font-size: 10px; color: #4c9b78; }
.cp-list { display: flex; flex-direction: column; gap: 2px; }
.cp-item { display: flex; align-items: center; gap: 8px; font-size: 11px; color: #b9c7d5; }
.cp-icon { width: 14px; text-align: center; font-size: 10px; }
.cp-icon.done { color: #34d399; }
.cp-icon.running { color: #fbbf24; }
.cp-icon.error { color: #f87171; }
.cp-icon.pending { color: #4f5d6d; }
.cp-label { white-space: nowrap; }
.cp-detail {
  color: #6b7f94;
  font-family: 'SF Mono', 'Cascadia Mono', 'Consolas', monospace;
  font-size: 10px;
}

.snap-bar {
  display: flex;
  align-items: center;
  gap: 8px;
  margin-bottom: 10px;
  padding: 5px 10px;
  background: rgba(15, 25, 41, 0.5);
  border: 1px solid rgba(74, 85, 104, 0.22);
  border-radius: 3px;
  font-size: 10.5px;
  color: #7d8c9e;
}
.snap-tag { color: #5b8ff9; font-weight: 600; letter-spacing: 1px; }
.snap-text { font-family: 'SF Mono', 'Cascadia Mono', 'Consolas', monospace; }
.snap-warn { margin-left: auto; color: #fbbf24; cursor: help; }

.main-head {
  display: flex;
  align-items: center;
  gap: 2px;
  margin-bottom: 10px;
  border-bottom: 1px solid rgba(74, 85, 104, 0.25);
}
.mh-tab {
  padding: 6px 14px;
  background: transparent;
  border: none;
  border-bottom: 2px solid transparent;
  color: #7d8c9e;
  font-family: inherit;
  font-size: 11.5px;
  cursor: pointer;
}
.mh-tab.active { color: #a5e0ff; border-bottom-color: #38bdf8; }
.mh-count {
  display: inline-block;
  margin-left: 4px;
  padding: 0 5px;
  border-radius: 8px;
  background: rgba(74, 85, 104, 0.3);
  font-size: 9.5px;
  font-family: 'SF Mono', Consolas, monospace;
}

/* ── Inspector ─────────────────────────────── */
.gate-inspector {
  min-height: 0;
  overflow: auto;
  border-left: 1px solid rgba(74, 85, 104, 0.25);
  background: #0b1117;
  position: relative;
}
.gate-inspector::-webkit-scrollbar { width: 5px; }
.gate-inspector::-webkit-scrollbar-thumb { background: #22303f; }

.inspector-toggle {
  position: absolute;
  top: 8px;
  right: 6px;
  z-index: 2;
  width: 18px;
  height: 18px;
  padding: 0;
  background: rgba(15, 25, 41, 0.9);
  border: 1px solid rgba(74, 85, 104, 0.35);
  border-radius: 3px;
  color: #7d8c9e;
  font-size: 11px;
  line-height: 16px;
  cursor: pointer;
}
.inspector-toggle:hover { color: #93c5fd; border-color: rgba(56, 189, 248, 0.4); }

.inspector-body { padding-top: 2px; }
.insp-sep { height: 1px; margin: 4px 12px; background: rgba(74, 85, 104, 0.2); }

/* ── 空态 ─────────────────────────────────── */
.stage-empty {
  display: flex;
  gap: 14px;
  margin: 6px 0;
  padding: 20px 24px;
  background: rgba(15, 25, 41, 0.6);
  border: 1px dashed rgba(74, 85, 104, 0.5);
  border-radius: 8px;
  align-items: flex-start;
}
.stage-icon {
  font-size: 32px;
  font-weight: 600;
  color: #5b8ff9;
  background: rgba(91, 143, 249, 0.12);
  border: 1px solid rgba(91, 143, 249, 0.35);
  width: 64px;
  height: 64px;
  border-radius: 12px;
  display: flex;
  align-items: center;
  justify-content: center;
  flex-shrink: 0;
}
.stage-empty h3 { margin: 6px 0 8px; font-size: 17px; color: #f1f5f9; font-weight: 600; }
.stage-empty p { margin: 0; color: #94a3b8; font-size: 12.5px; line-height: 1.6; max-width: 620px; }
</style>
