<template>
  <div class="feature-gate-panel">
    <!-- 门禁配置 -->
    <div class="config-section">
      <div class="section-heading">
        <div>
          <span class="section-eyebrow">FEATURE GATE</span>
          <h3 class="section-title">特征门禁</h3>
        </div>
        <span class="section-hint">
          DEV 内滚动 {{ config.nFolds }} 折，折间隔离 {{ effectiveGap }} bar
        </span>
      </div>

      <div class="config-grid">
        <div class="config-item">
          <label>
            交叉验证折数 N
            <span class="tip-icon" title="DEV 区间内切 N 个连续折，报每折 IC 与同号折数。&#10;折数越多，单折样本越少、门槛越难达到。">?</span>
          </label>
          <input type="number" v-model.number="config.nFolds" min="2" max="50" class="select-input"
                 @change="onNFoldsChange" />
          <span class="config-hint">2 ~ 50</span>
        </div>

        <div class="config-item">
          <label>
            折间间隔 gap
            <span class="tip-icon" title="相邻折之间被完全跳过、不参与任何折的 bar 数。&#10;标签是 labelPeriod 根 bar 的前瞻收益，至少要隔这么多，两折的标签窗口才不重叠。&#10;设定值小于标签周期时，系统会自动抬到标签周期。">?</span>
          </label>
          <input type="number" v-model.number="config.foldGap" min="0" max="250" class="select-input" />
          <span class="config-hint" :class="{ warn: gapRaised }">
            设定 {{ config.foldGap }} bar · 生效 {{ effectiveGap }} bar
            <template v-if="gapRaised">（受标签周期 {{ config.labelPeriod }} 约束）</template>
          </span>
        </div>

        <div class="config-item">
          <label>
            标签波动率口径
            <span class="tip-icon" title="标签阈值的 σ 口径。&#10;expanding：逐行因果，阈值只用该行之前的数据，无前视泄露。&#10;full：全序列一个常数（旧行为，早期行的阈值里含后期波动率）。">?</span>
          </label>
          <select v-model="config.sigmaMode" class="select-input">
            <option value="expanding">expanding（因果，无泄露）</option>
            <option value="full">full（全序列，旧行为）</option>
          </select>
          <span class="config-hint" :class="{ warn: config.sigmaMode === 'full' }">
            {{ config.sigmaMode === 'full' ? '⚠ 全序列口径存在前视泄露' : '仅影响后续三分类训练，不改变 rank IC' }}
          </span>
        </div>
      </div>

      <!-- 门禁规则（折叠） -->
      <button class="advanced-toggle" @click="showRules = !showRules">
        <i :class="showRules ? 'fas fa-chevron-down' : 'fas fa-chevron-right'"></i>
        门禁规则
        <span class="advanced-hint">预注册选择条件，只看 DEV</span>
      </button>
      <div v-show="showRules" class="advanced-body">
        <div class="config-grid">
          <div class="config-item">
            <label>|IC| 门槛</label>
            <input type="number" v-model.number="rules.minIc" step="0.005" min="0" max="1" class="select-input" />
          </div>
          <div class="config-item">
            <label>|NW t| 门槛</label>
            <input type="number" v-model.number="rules.minT" step="0.1" min="0" max="50" class="select-input" />
          </div>
          <div class="config-item">
            <label>同号折数</label>
            <input type="number" v-model.number="rules.minSignFolds" min="0" :max="config.nFolds" class="select-input" />
            <span class="config-hint">0 ~ {{ config.nFolds }}</span>
          </div>
          <div class="config-item">
            <label>FDR q 门槛</label>
            <input type="number" v-model.number="rules.fdrQ" step="0.01" min="0.01" max="1" class="select-input" />
          </div>
          <div class="config-item">
            <label>覆盖率门槛</label>
            <input type="number" v-model.number="rules.minCoverage" step="0.05" min="0" max="1" class="select-input" />
          </div>
          <div class="config-item">
            <label>top-k 门槛</label>
            <input type="number" v-model.number="rules.gateTopk" min="1" max="50" class="select-input" />
            <span class="config-hint">top-k 超额必须 &gt; 0</span>
          </div>
          <div class="config-item">
            <label>单日最少标的</label>
            <input type="number" v-model.number="rules.minNames" min="2" max="500" class="select-input" />
          </div>
          <div class="config-item">
            <label>历史试验次数</label>
            <input type="number" v-model.number="rules.trialCount" min="0" max="100000" class="select-input" />
            <span class="config-hint">用于校正 FDR，留 0 表示按本次特征数计</span>
          </div>
        </div>
        <div class="config-grid" style="margin-top: 10px">
          <label class="check-item">
            <input type="checkbox" v-model="options.noTest" />
            <span>不计算锁定 TEST（只看 DEV）</span>
          </label>
          <label class="check-item">
            <input type="checkbox" v-model="options.noIncremental" />
            <span>跳过增量 IC（不与现有特征做残差回归）</span>
          </label>
          <label class="check-item">
            <input type="checkbox" v-model="options.noLagSensitivity" />
            <span>跳过滞后敏感性诊断</span>
          </label>
        </div>
      </div>
    </div>

    <!-- 动作 -->
    <div class="gate-action">
      <button class="btn btn-primary"
              :disabled="state.screenState.loading || !csvPath"
              @click="onScreen">
        <i v-if="state.screenState.loading" class="fas fa-spinner fa-spin"></i>
        {{ state.screenState.loading ? '门禁中…' : '开始门禁' }}
      </button>
      <span v-if="!csvPath" class="gate-hint warn">
        <i class="fas fa-exclamation-circle"></i>请先执行「开始收集」——门禁需要服务端生成的特征缓存
      </span>
      <span v-else class="gate-hint">
        <i class="fas fa-database"></i>特征缓存就绪（collect 产出）
      </span>
    </div>

    <!-- 进度 -->
    <div v-if="state.screenState.steps.length > 0" class="training-progress">
      <div class="progress-header">
        <span class="progress-title">门禁进度</span>
        <span v-if="!state.screenState.loading" class="progress-done">完成</span>
      </div>
      <div class="step-list">
        <div v-for="step in state.screenState.steps" :key="step.id" class="step-item">
          <span class="step-icon" :class="step.status">
            <template v-if="step.status === 'done'">✓</template>
            <template v-else-if="step.status === 'running'">⏳</template>
            <template v-else-if="step.status === 'error'">✗</template>
            <template v-else>○</template>
          </span>
          <span class="step-label">{{ step.label }}</span>
          <span v-if="step.detail" class="step-detail">{{ step.detail }}</span>
        </div>
      </div>
    </div>

    <!-- 汇总 -->
    <div v-if="summary" class="summary-bar">
      <div class="summary-item">
        <span class="summary-label">特征数</span>
        <span class="summary-value">{{ summary.n_features }}</span>
      </div>
      <div class="summary-item">
        <span class="summary-label">A 档</span>
        <span class="summary-value good">{{ summary.n_tier_a }}</span>
      </div>
      <div class="summary-item">
        <span class="summary-label">B 档</span>
        <span class="summary-value">{{ summary.n_tier_b }}</span>
      </div>
      <div class="summary-item">
        <span class="summary-label">多重检验 M</span>
        <span class="summary-value">{{ summary.m_tests }}</span>
      </div>
      <div class="summary-item">
        <span class="summary-label">Bonferroni t</span>
        <span class="summary-value">{{ fmt(summary.bonferroni_t, 2) }}</span>
      </div>
      <div class="summary-item">
        <span class="summary-label">DEV / TEST</span>
        <span class="summary-value">{{ summary.dev_days }} / {{ summary.test_days }} 天</span>
      </div>
    </div>

    <!-- 结论表 -->
    <div v-if="rows.length > 0" class="chart-section">
      <div class="section-heading">
        <div>
          <span class="section-eyebrow">GATE RESULT</span>
          <h3 class="section-title">门禁结论</h3>
        </div>
        <div class="table-actions">
          <span class="section-hint">A=全部通过 · B=原始规则通过但 FDR 未过 · C=拒绝</span>
          <button class="btn btn-ghost" @click="copySelected">复制选中特征名</button>
        </div>
      </div>

      <div class="feature-table-wrapper">
        <table class="feature-table">
          <thead>
            <tr>
              <th class="col-check"><input type="checkbox" :checked="allSelected" @change="toggleAll" /></th>
              <th>特征</th>
              <th>方向</th>
              <th>DEV IC</th>
              <th>NW t</th>
              <th>q 值</th>
              <th>同号折</th>
              <th>覆盖</th>
              <th>结论</th>
              <th>TEST IC</th>
              <th></th>
            </tr>
          </thead>
          <tbody>
            <template v-for="row in rows" :key="row.name">
              <tr :class="rowClass(row)">
                <td class="col-check">
                  <input type="checkbox" :checked="selected.has(row.name)" @change="toggleOne(row.name)" />
                </td>
                <td class="feature-name" :title="row.name">{{ row.name }}</td>
                <td>
                  <span class="dir-tag" :class="row.direction">{{ row.direction === 'positive' ? '正向' : '反向' }}</span>
                </td>
                <td :class="icClass(row.dev.ic_mean)">{{ fmt(row.dev.ic_mean, 4) }}</td>
                <td :class="icClass(row.dev.nw_t)">{{ fmt(row.dev.nw_t, 2) }}</td>
                <td>{{ fmt(row.q_value, 3) }}</td>
                <td>{{ row.signFolds }} / {{ nFolds }}</td>
                <td>{{ fmt(row.dev.coverage * 100, 0) }}%</td>
                <td>
                  <span class="tier-tag" :class="row.tier">{{ row.tier }}</span>
                  <span v-if="row.reasons.length" class="reason-text" :title="row.reasons.join('；')">
                    {{ row.reasons.join('；') }}
                  </span>
                </td>
                <td :class="row.test ? icClass(row.test.ic_mean) : ''">
                  {{ row.test ? fmt(row.test.ic_mean, 4) : '—' }}
                </td>
                <td class="col-expand">
                  <button class="expand-btn" @click="toggleExpand(row.name)">
                    <i :class="expanded.has(row.name) ? 'fas fa-chevron-up' : 'fas fa-chevron-down'"></i>
                  </button>
                </td>
              </tr>
              <tr v-if="expanded.has(row.name)" class="detail-row">
                <td colspan="11">
                  <div class="detail-grid">
                    <div class="detail-block">
                      <div class="detail-title">逐折 IC</div>
                      <div class="fold-chips">
                        <span v-for="(ic, i) in row.foldIc" :key="i" class="fold-chip"
                              :class="{ neg: ic != null && ic < 0 }">
                          折{{ i + 1 }} {{ ic == null ? '—' : fmt(ic, 4) }}
                        </span>
                      </div>
                      <div v-if="row.foldDates?.length" class="fold-dates">
                        <span v-for="(d, i) in row.foldDates" :key="i" class="fold-date">
                          折{{ i + 1 }}: {{ d ? shortDate(d[0]) : '' }} ~ {{ d ? shortDate(d[1]) : '' }}
                        </span>
                      </div>
                    </div>

                    <div class="detail-block">
                      <div class="detail-title">IC 衰减（按 horizon）</div>
                      <div class="fold-chips">
                        <span v-for="(d, h) in row.decay" :key="h" class="fold-chip">
                          h={{ h }} {{ fmt(d.ic, 4) }} <span class="dim">(t {{ fmt(d.t, 2) }})</span>
                        </span>
                      </div>
                    </div>

                    <div class="detail-block">
                      <div class="detail-title">增量 IC（对现有特征残差）</div>
                      <div class="detail-text">
                        <template v-if="row.incremental && isFiniteNum(row.incremental.incr_ic)">
                          IC {{ fmt(row.incremental.incr_ic, 4) }}
                          <span class="dim">(t {{ fmt(row.incremental.incr_t, 2) }})</span>
                          · 保留比例 {{ fmt(row.incremental.incr_ratio, 2) }}
                          · 有效 {{ row.incremental.incr_days }} 天
                        </template>
                        <template v-else>未计算</template>
                      </div>
                    </div>

                    <div class="detail-block">
                      <div class="detail-title">逐年 IC（DEV）</div>
                      <div class="fold-chips">
                        <span v-for="(ic, y) in row.yearly" :key="y" class="fold-chip" :class="{ neg: ic < 0 }">
                          {{ y }} {{ fmt(ic, 4) }}
                        </span>
                      </div>
                    </div>

                    <div v-if="row.lag" class="detail-block">
                      <div class="detail-title">滞后敏感性</div>
                      <div class="detail-text">
                        <template v-if="row.lag.ic">
                          <span v-for="(ic, k) in row.lag.ic" :key="k" class="dim">
                            lag {{ k }}: {{ fmt(ic, 4) }}&nbsp;
                          </span>
                          · 自相关(1) {{ fmt(row.lag.autocorr1, 2) }}
                          <span v-if="row.lag.verdict" class="dim">· {{ row.lag.verdict }}</span>
                        </template>
                        <template v-else>未计算</template>
                      </div>
                    </div>
                  </div>
                </td>
              </tr>
            </template>
          </tbody>
        </table>
      </div>
    </div>

    <div v-if="!rows.length && !state.screenState.loading && state.screenState.task" class="empty-state">
      <div class="empty-icon">🚦</div>
      <div>门禁没有产出特征结果</div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, reactive, computed, watch } from 'vue'
import { ElMessage } from 'element-plus'
import { useMLData } from '../composables/useMLData'
import type { ScreenFeature, TrainStep } from '../composables/useMLState'

const props = defineProps<{
  state: any
  csvPath?: string
}>()

const { screen, screenResult } = useMLData()
const config = props.state.config

const showRules = ref(false)
const selected = ref(new Set<string>())
const expanded = ref(new Set<string>())

const rules = reactive({
  minIc: 0.02,
  minT: 2.5,
  minSignFolds: 4,
  fdrQ: 0.10,
  minCoverage: 0.5,
  gateTopk: 5,
  minNames: 15,
  trialCount: 0,
})

const options = reactive({
  noTest: false,
  noIncremental: false,
  noLagSensitivity: false,
})

const STEP_LABELS: Record<string, string> = {
  prepare: '准备',
  parse_script: '解析策略图',
  init_nodes: '初始化节点',
  start_exchange: '启动数据源',
  collect_data: '收集特征数据',
  analyze: '分析特征数据',
  validate: '校验缓存',
  load_cache: '加载特征缓存',
  compute_target: '计算目标',
  evaluate: '逐特征评估',
  multiple_testing: '多重检验',
  report: '生成报告',
  screen: '特征门禁',
}

const csvPath = computed(() => props.csvPath || '')
const nFolds = computed(() => config.nFolds)
const effectiveGap = computed(() => Math.max(config.foldGap || 0, config.labelPeriod || 0))
const gapRaised = computed(() => (config.foldGap || 0) < (config.labelPeriod || 0))

const summary = computed(() => props.state.screenState.task?.summary || null)

interface GateRow {
  name: string
  tier: 'A' | 'B' | 'C'
  direction: 'positive' | 'negative'
  q_value: number | null
  reasons: string[]
  dev: any
  test: any
  incremental: any
  decay: Record<string, { ic: number; t: number }>
  yearly: Record<string, number>
  lag: any
  foldIc: (number | null)[]
  foldDates: (string[] | null)[] | null
  signFolds: number
}

const rows = computed<GateRow[]>(() => {
  const report = props.state.screenState.report
  if (!report || !report.features) return []
  const out: GateRow[] = []
  for (const [name, f] of Object.entries(report.features) as [string, ScreenFeature][]) {
    const folds = f.dev?.folds
    out.push({
      name,
      tier: f.selection?.tier || 'C',
      direction: (f.sign ?? 1) > 0 ? 'positive' : 'negative',
      q_value: f.q_value ?? null,
      reasons: f.selection?.reasons || [],
      dev: f.dev,
      test: f.test,
      incremental: f.incremental,
      decay: f.decay || {},
      yearly: f.dev?.yearly || {},
      lag: f.lag,
      foldIc: folds?.fold_ic || [],
      foldDates: folds?.fold_dates || null,
      signFolds: folds?.sign_consistency ?? 0,
    })
  }
  // A 档在前，其次按 |DEV IC| 降序
  out.sort((a, b) => {
    if (a.tier !== b.tier) return a.tier < b.tier ? -1 : 1
    return Math.abs(b.dev?.ic_mean || 0) - Math.abs(a.dev?.ic_mean || 0)
  })
  return out
})

const allSelected = computed(() => rows.value.length > 0 && rows.value.every((r) => selected.value.has(r.name)))

function onNFoldsChange() {
  config.nFolds = Math.min(Math.max(Number(config.nFolds) || 2, 2), 50)
  // 服务端要求 min_sign_folds <= n_folds
  if (rules.minSignFolds > config.nFolds) rules.minSignFolds = config.nFolds
}

function toggleOne(name: string) {
  const next = new Set(selected.value)
  next.has(name) ? next.delete(name) : next.add(name)
  selected.value = next
}

function toggleAll() {
  if (allSelected.value) {
    selected.value = new Set()
  } else {
    selected.value = new Set(rows.value.map((r) => r.name))
  }
}

function toggleExpand(name: string) {
  const next = new Set(expanded.value)
  next.has(name) ? next.delete(name) : next.add(name)
  expanded.value = next
}

function copySelected() {
  const names = rows.value.filter((r) => selected.value.has(r.name)).map((r) => r.name)
  if (!names.length) {
    ElMessage.warning('请先勾选要复制的特征')
    return
  }
  navigator.clipboard?.writeText(names.join(',')).then(
    () => ElMessage.success(`已复制 ${names.length} 个特征名`),
    () => ElMessage.error('复制失败'),
  )
}

function fmt(v: any, nd = 4): string {
  if (v == null || !isFiniteNum(v)) return '—'
  return Number(v).toFixed(nd)
}

/** '2024-03-18 00:00:00' → '2024-03-18' */
function shortDate(s: string): string {
  return typeof s === 'string' ? s.split(' ')[0] : String(s ?? '')
}

function isFiniteNum(v: any): boolean {
  return typeof v === 'number' && isFinite(v)
}

function icClass(v: any): string {
  if (!isFiniteNum(v)) return ''
  return v >= 0 ? 'pos' : 'neg'
}

function rowClass(row: GateRow): string {
  return row.tier === 'A' ? 'tier-a' : row.tier === 'B' ? 'tier-b' : ''
}

// ── 发起门禁 ────────────────────────────────────────────────
async function onScreen() {
  if (!csvPath.value) {
    ElMessage.warning('请先执行「开始收集」，门禁需要服务端生成的特征缓存')
    return
  }
  const st = props.state.screenState
  st.loading = true
  st.task = null
  st.report = null
  st.steps = []
  st.logs = []
  st.error = ''
  selected.value = new Set()
  expanded.value = new Set()

  try {
    const task = await screen(
      csvPath.value,
      {
        nFolds: config.nFolds,
        foldGap: config.foldGap,
        primaryHorizon: config.labelPeriod,
        labelPeriod: config.labelPeriod,
        volK: config.volK,
        sigmaMode: config.sigmaMode,
        minIc: rules.minIc,
        minT: rules.minT,
        minSignFolds: rules.minSignFolds,
        fdrQ: rules.fdrQ,
        minCoverage: rules.minCoverage,
        gateTopk: rules.gateTopk,
        minNames: rules.minNames,
        trialCount: rules.trialCount,
        noTest: options.noTest,
        noIncremental: options.noIncremental,
        noLagSensitivity: options.noLagSensitivity,
      },
      (type: string, data: any) => {
        if (type === 'step') {
          const stepId = data.step
          const existing = st.steps.find((s: TrainStep) => s.id === stepId)
          if (data.status === 'start') {
            if (!existing) {
              st.steps.push({
                id: stepId,
                label: STEP_LABELS[stepId] || stepId,
                status: 'running',
                detail: data.msg,
              })
            }
          } else if (data.status === 'done' && existing) {
            existing.status = 'done'
          }
        } else if (type === 'progress') {
          const step = st.steps.find((s: TrainStep) => s.id === data.step)
          if (step) {
            step.detail = data.total ? `${data.current} / ${data.total} ${data.feature || ''}` : String(data.current ?? '')
          } else {
            st.steps.push({
              id: 'evaluate',
              label: STEP_LABELS.evaluate,
              status: 'running',
              detail: data.total ? `${data.current} / ${data.total} ${data.feature || ''}` : '',
            })
          }
        } else if (type === 'error') {
          st.error = data.msg || data.message || '门禁失败'
        }
      },
    )

    if (!task) {
      st.error = st.error || '门禁未返回结果'
      return
    }
    st.task = task

    // 逐特征明细在 feature_screen.json 里，result 事件只带汇总
    const report = await screenResult(task.run_id)
    if (report) {
      st.report = report
      // 默认勾选 A 档
      const aTier = Object.entries(report.features)
        .filter(([, f]) => (f as ScreenFeature).selection?.tier === 'A')
        .map(([name]) => name)
      selected.value = new Set(aTier)
    }
  } catch (err: any) {
    st.error = err?.message || '门禁失败'
  } finally {
    st.loading = false
  }
}

// 门禁重跑后清空旧的勾选与展开状态
watch(() => props.state.screenState.task, () => {
  selected.value = new Set()
  expanded.value = new Set()
})
</script>

<style scoped>
.feature-gate-panel { padding: 4px 0 16px; }

.config-section {
  margin-bottom: 14px;
  padding: 14px 16px 16px;
  background: rgba(15, 25, 41, 0.55);
  border: 1px solid rgba(74, 85, 104, 0.25);
  border-radius: 8px;
}
.section-heading {
  display: flex;
  justify-content: space-between;
  align-items: flex-end;
  margin-bottom: 12px;
}
.section-eyebrow {
  font-size: 10.5px;
  letter-spacing: 2.2px;
  color: #5b8ff9;
  font-weight: 600;
  text-transform: uppercase;
}
.section-title { color: #e2e8f0; font-size: 15px; margin: 4px 0 0; font-weight: 600; }
.section-hint { font-size: 11.5px; color: #94a3b8; }

.config-grid {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(220px, 1fr));
  gap: 12px;
}
.config-item { display: flex; flex-direction: column; gap: 5px; }
.config-item > label {
  display: flex;
  align-items: center;
  gap: 5px;
  font-size: 11.5px;
  color: #94a3b8;
}
.tip-icon {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  width: 13px;
  height: 13px;
  border-radius: 50%;
  background: rgba(91, 143, 249, 0.2);
  color: #93c5fd;
  font-size: 9px;
  cursor: help;
  font-weight: 700;
}
.select-input {
  width: 100%;
  padding: 6px 9px;
  background: rgba(10, 18, 32, 0.8);
  border: 1px solid rgba(74, 85, 104, 0.4);
  border-radius: 4px;
  color: #e2e8f0;
  font-size: 12px;
  font-family: inherit;
}
.select-input:focus { outline: none; border-color: #3b82f6; }
.config-hint { font-size: 10.5px; color: #64748b; }
.config-hint.warn { color: #fbbf24; }

.advanced-toggle {
  margin-top: 12px;
  width: 100%;
  display: flex;
  align-items: center;
  gap: 7px;
  padding: 7px 10px;
  background: rgba(91, 143, 249, 0.07);
  border: 1px solid rgba(74, 85, 104, 0.28);
  border-radius: 5px;
  color: #cbd5e1;
  font-size: 12px;
  cursor: pointer;
}
.advanced-hint { margin-left: auto; font-size: 10.5px; color: #64748b; }
.advanced-body { margin-top: 12px; }

.check-item {
  display: flex;
  align-items: center;
  gap: 6px;
  font-size: 11.5px;
  color: #94a3b8;
  cursor: pointer;
}
.check-item input { accent-color: #3b82f6; }

.gate-action { display: flex; align-items: center; gap: 10px; margin-bottom: 14px; }
.btn {
  padding: 6px 16px;
  border-radius: 4px;
  font-size: 12px;
  cursor: pointer;
  border: none;
  transition: all 0.2s;
}
.btn-primary { background: #3b82f6; color: white; }
.btn-primary:hover:not(:disabled) { background: #2563eb; }
.btn-primary:disabled { background: #475569; cursor: not-allowed; opacity: 0.7; }
.btn-ghost {
  background: rgba(91, 143, 249, 0.12);
  color: #93c5fd;
  border: 1px solid rgba(91, 143, 249, 0.3);
}
.btn-ghost:hover { background: rgba(91, 143, 249, 0.2); }
.gate-hint { font-size: 11.5px; color: #64748b; display: flex; align-items: center; gap: 5px; }
.gate-hint.warn { color: #fbbf24; }

.training-progress {
  margin-bottom: 16px;
  background: rgba(15, 25, 41, 0.55);
  border: 1px solid rgba(74, 85, 104, 0.25);
  border-radius: 8px;
  padding: 12px 16px;
}
.progress-header { display: flex; justify-content: space-between; align-items: center; margin-bottom: 10px; }
.progress-title { font-size: 13px; font-weight: 600; color: #e2e8f0; }
.progress-done { font-size: 11px; color: #34d399; font-weight: 600; }
.step-list { display: flex; flex-direction: column; gap: 4px; }
.step-item { display: flex; align-items: center; gap: 8px; font-size: 12px; padding: 3px 0; }
.step-icon { width: 18px; text-align: center; font-size: 12px; flex-shrink: 0; }
.step-icon.done { color: #34d399; }
.step-icon.running { color: #fbbf24; }
.step-icon.error { color: #f87171; }
.step-icon.pending { color: #64748b; }
.step-label { color: #cbd5e1; white-space: nowrap; }
.step-detail { color: #94a3b8; font-size: 11px; font-family: 'SF Mono', 'Consolas', monospace; }

.summary-bar {
  display: flex;
  flex-wrap: wrap;
  gap: 22px;
  padding: 12px 16px;
  margin-bottom: 14px;
  background: rgba(15, 25, 41, 0.55);
  border: 1px solid rgba(74, 85, 104, 0.25);
  border-radius: 8px;
}
.summary-item { display: flex; flex-direction: column; gap: 3px; }
.summary-label { font-size: 10.5px; color: #64748b; }
.summary-value { font-size: 15px; color: #e2e8f0; font-weight: 600; font-family: 'SF Mono', 'Consolas', monospace; }
.summary-value.good { color: #34d399; }

.chart-section {
  margin-bottom: 18px;
  background: rgba(15, 25, 41, 0.55);
  border: 1px solid rgba(74, 85, 104, 0.25);
  border-radius: 8px;
  padding: 14px 16px 16px;
}
.table-actions { display: flex; align-items: center; gap: 12px; }

.feature-table-wrapper { overflow-x: auto; max-height: 560px; overflow-y: auto; }
.feature-table { width: 100%; border-collapse: collapse; font-size: 11.5px; }
.feature-table th {
  position: sticky;
  top: 0;
  z-index: 1;
  background: rgba(19, 28, 46, 0.97);
  color: #94a3b8;
  font-weight: 600;
  text-align: left;
  padding: 7px 8px;
  border-bottom: 1px solid rgba(74, 85, 104, 0.35);
  white-space: nowrap;
}
.feature-table td {
  padding: 6px 8px;
  border-bottom: 1px solid rgba(74, 85, 104, 0.16);
  color: #cbd5e1;
  white-space: nowrap;
}
.feature-table tbody tr.tier-a { background: rgba(52, 211, 153, 0.06); }
.feature-table tbody tr.tier-b { background: rgba(251, 191, 36, 0.05); }
.feature-name { color: #e2e8f0; font-family: 'SF Mono', 'Consolas', monospace; max-width: 180px; overflow: hidden; text-overflow: ellipsis; }
td.pos { color: #34d399; }
td.neg { color: #f87171; }
.col-check { width: 28px; }
.col-check input { accent-color: #3b82f6; cursor: pointer; }
.col-expand { width: 26px; text-align: center; }
.expand-btn {
  background: none;
  border: none;
  color: #64748b;
  cursor: pointer;
  font-size: 11px;
  padding: 2px 4px;
}
.expand-btn:hover { color: #93c5fd; }

.dir-tag {
  display: inline-block;
  padding: 1px 6px;
  border-radius: 3px;
  font-size: 10px;
}
.dir-tag.positive { background: rgba(52, 211, 153, 0.14); color: #34d399; }
.dir-tag.negative { background: rgba(248, 113, 113, 0.14); color: #f87171; }

.tier-tag {
  display: inline-block;
  min-width: 16px;
  text-align: center;
  padding: 1px 5px;
  border-radius: 3px;
  font-size: 10px;
  font-weight: 700;
}
.tier-tag.A { background: rgba(52, 211, 153, 0.18); color: #34d399; }
.tier-tag.B { background: rgba(251, 191, 36, 0.18); color: #fbbf24; }
.tier-tag.C { background: rgba(100, 116, 139, 0.2); color: #94a3b8; }
.reason-text {
  margin-left: 6px;
  font-size: 10.5px;
  color: #64748b;
  max-width: 240px;
  overflow: hidden;
  text-overflow: ellipsis;
  display: inline-block;
  vertical-align: bottom;
}

.detail-row td { background: rgba(10, 18, 32, 0.55); white-space: normal; padding: 10px 12px; }
.detail-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(280px, 1fr)); gap: 12px; }
.detail-block { display: flex; flex-direction: column; gap: 5px; }
.detail-title { font-size: 10.5px; color: #5b8ff9; font-weight: 600; letter-spacing: 0.5px; }
.detail-text { font-size: 11.5px; color: #cbd5e1; }
.dim { color: #64748b; }
.fold-chips { display: flex; flex-wrap: wrap; gap: 5px; }
.fold-chip {
  padding: 2px 7px;
  border-radius: 3px;
  background: rgba(91, 143, 249, 0.1);
  color: #93c5fd;
  font-size: 10.5px;
  font-family: 'SF Mono', 'Consolas', monospace;
}
.fold-chip.neg { background: rgba(248, 113, 113, 0.12); color: #f87171; }
.fold-dates { display: flex; flex-wrap: wrap; gap: 8px; }
.fold-date { font-size: 10px; color: #64748b; font-family: 'SF Mono', 'Consolas', monospace; }

.empty-state { padding: 40px 20px; text-align: center; color: #64748b; }
.empty-icon { font-size: 40px; margin-bottom: 10px; }
</style>
