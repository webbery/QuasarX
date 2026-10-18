<template>
  <div class="stability-panel">
    <!-- 实验配置区 -->
    <div class="config-card">
      <div class="config-header">
        <h3><i class="fas fa-sliders-h"></i> 稳定性实验配置</h3>
        <span class="pending-tag" :class="{ live: hasRunnable }">
          <i :class="hasRunnable ? 'fas fa-circle-play' : 'fas fa-hourglass-half'"></i>
          {{ hasRunnable ? '部分已接入' : '待后端支持' }}
        </span>
      </div>

      <div class="config-body">
        <!-- 维度勾选 -->
        <div class="field">
          <label>扰动维度</label>
          <div class="dimension-checks">
            <label v-for="dim in dimensions" :key="dim.id" class="check-item">
              <input type="checkbox" :value="dim.id" v-model="selectedDimensionIds" />
              <span class="check-text">{{ dim.label }}</span>
            </label>
          </div>
        </div>

        <!-- 子检验 -->
        <div class="field">
          <label>子检验</label>
          <div class="subcheck-list">
            <label
              v-for="sub in availableSubChecks"
              :key="sub.dimensionId + '-' + sub.id"
              class="check-item"
              :class="{ disabled: !sub.implemented }"
            >
              <input
                type="checkbox"
                :value="sub.key"
                v-model="selectedSubCheckKeys"
                :disabled="!sub.implemented"
              />
              <span class="check-text">{{ sub.label }}</span>
              <span class="impl-tag" :class="{ on: sub.implemented }">
                {{ sub.implemented ? '已接入' : '待接入' }}
              </span>
            </label>
          </div>
        </div>

        <!-- 待扫描参数（C 组） -->
        <div v-if="isSelected('param:grid')" class="field">
          <label>
            待扫描参数
            <span class="label-hint">自动提取策略图中的数值型参数，一次只扰动一个</span>
          </label>
          <div v-if="perturbableParams.length === 0" class="no-params">
            当前策略图未找到数值型参数
          </div>
          <div v-else class="param-checks">
            <label
              v-for="p in perturbableParams"
              :key="`${p.nodeId}:${p.key}`"
              class="check-item"
            >
              <input
                type="checkbox"
                :value="paramKey(p)"
                v-model="selectedParamKeys"
              />
              <span class="check-text">
                {{ p.nodeLabel }} / {{ p.label }}
                <span class="param-meta">
                  基准 {{ p.value }} · 步长 {{ p.step }}<template v-if="p.min !== undefined || p.max !== undefined"> · [{{ p.min ?? '-∞' }}, {{ p.max ?? '+∞' }}]</template>
                </span>
              </span>
            </label>
          </div>
        </div>

        <!-- 全局运行参数 -->
        <div class="config-inline">
          <div class="inline-field">
            <label>运行间隔 (ms)</label>
            <input type="number" class="num-input" v-model.number="runInterval" min="0" step="100" />
          </div>
          <div class="inline-field">
            <label>起点偏移步长 (bar)</label>
            <input type="number" class="num-input" v-model.number="startStepBars" min="1" step="1" />
          </div>
          <div class="inline-field">
            <label>尾部保护 (bar)</label>
            <input type="number" class="num-input" v-model.number="startTailGuard" min="0" step="21" />
          </div>
          <div class="inline-field">
            <label>参数网格点数</label>
            <input type="number" class="num-input" v-model.number="paramGridSize" min="3" step="2" />
          </div>
          <div class="inline-field wide">
            <label>资金档位上限来源</label>
            <div class="pool-hint">
              {{ poolHint }}
            </div>
          </div>
        </div>

        <div class="config-actions">
          <button class="btn btn-primary" :disabled="!canRun || running" @click="handleRun">
            <i :class="running ? 'fas fa-spinner fa-spin' : 'fas fa-play'"></i>
            {{ running ? `运行中 ${progress.done}/${progress.total}${progress.label ? ' · ' + progress.label : ''}` : '运行稳定性分析' }}
          </button>
          <button v-if="running" class="btn btn-ghost" @click="abortFlag = true">中止</button>
          <span class="run-hint">{{ runHint }}</span>
        </div>
      </div>
    </div>

    <!-- 资金规模不变性结果 -->
    <div v-if="sweepResult" class="result-card">
      <div class="result-header">
        <h4>资金规模不变性</h4>
        <span class="verdict-badge" :class="sweepResult.verdict.level">
          {{ verdictLabel(sweepResult.verdict.level) }}
        </span>
      </div>

      <p class="verdict-text">{{ sweepResult.verdict.text }}</p>

      <div class="slippage-bar" :class="{ off: !sweepResult.slippageModeled }">
        <i class="fas fa-circle-info"></i>
        <span>
          滑点模型：
          <template v-if="!sweepResult.slippage.found">未找到执行节点</template>
          <template v-else-if="sweepResult.slippage.modelType === 1">
            成交量冲击（base={{ sweepResult.slippage.base }}，k={{ sweepResult.slippage.impactK }}，α={{ sweepResult.slippage.alpha }}）
          </template>
          <template v-else>
            固定比例（ratio={{ sweepResult.slippage.fixedRatio }}）
          </template>
          ；最低手续费 {{ sweepResult.slippage.minFee }} 元；归一化基准档 {{ formatCapital(sweepResult.baselineCapital) }}
        </span>
      </div>

      <table class="result-table">
        <thead>
          <tr>
            <th>资金档</th>
            <th>总收益率</th>
            <th>年化收益</th>
            <th>夏普</th>
            <th>最大回撤</th>
            <th>笔数</th>
            <th>换手率</th>
            <th>R(c)/R(base)</th>
            <th>N(c)/N(base)</th>
          </tr>
        </thead>
        <tbody>
          <tr v-for="run in sweepResult.runs" :key="run.capital" :class="{ failed: !run.ok, baseline: run.capital === sweepResult.baselineCapital }">
            <td>{{ formatCapital(run.capital) }}</td>
            <template v-if="run.ok">
              <td :class="valueClass(run.totalReturn)">{{ formatPct(run.totalReturn) }}</td>
              <td :class="valueClass(run.annualReturn)">{{ formatPct(run.annualReturn) }}</td>
              <td>{{ run.sharp === null ? '-' : run.sharp.toFixed(2) }}</td>
              <td>{{ formatPct(run.maxDrawdown) }}</td>
              <td>{{ run.tradeCount }}</td>
              <td>{{ formatNum(run.turnover) }}</td>
              <td>{{ formatNum(ratioOf(sweepResult.normalized, run.capital)) }}</td>
              <td>{{ formatNum(ratioOf(sweepResult.tradeRatio, run.capital)) }}</td>
            </template>
            <td v-else colspan="8" class="error-cell">{{ run.error }}</td>
          </tr>
        </tbody>
      </table>

      <p class="epsilon-line">
        资金敏感性 ε = {{ sweepResult.epsilon === null ? '-' : sweepResult.epsilon.toFixed(3) }}
        （固定比例滑点模型下应接近 0；成交量冲击模型下应随 c 递减）
      </p>
    </div>

    <div v-else-if="sweepError" class="result-card error">
      <p class="verdict-text">{{ sweepError }}</p>
    </div>

    <!-- 回测起点不变性结果 -->
    <div v-if="startResult" class="result-card">
      <div class="result-header">
        <h4>回测起点不变性</h4>
        <span class="verdict-badge" :class="startResult.analysis.verdict.level">
          {{ verdictLabel(startResult.analysis.verdict.level) }}
        </span>
      </div>

      <p class="verdict-text">{{ startResult.analysis.verdict.text }}</p>

      <div class="slippage-bar">
        <i class="fas fa-circle-info"></i>
        <span>
          共 {{ startResult.analysis.count }} 个起点 · 步长 {{ startResult.stepBars }} bar · 尾部保护
          {{ startResult.tailGuardBars }} bar · end 固定于 {{ startResult.endDate || '-' }} · 锁死本金
          {{ formatCapital(startResult.baseCapital) }} · 变化性质
          <strong>{{ classificationLabel(startResult.analysis.structure.classification) }}</strong>
        </span>
      </div>

      <div class="stat-row">
        <div class="stat-cell">
          <span class="stat-label">相对极差</span>
          <span class="stat-value">{{ formatNum(startResult.analysis.amplitude.relativeRange) }}</span>
        </div>
        <div class="stat-cell">
          <span class="stat-label">最大相邻跳变</span>
          <span class="stat-value">{{ formatNum(startResult.analysis.amplitude.maxJump) }}</span>
        </div>
        <div class="stat-cell">
          <span class="stat-label">lag-1 自相关</span>
          <span class="stat-value">{{ formatNum(startResult.analysis.structure.acf1) }}</span>
        </div>
        <div class="stat-cell">
          <span class="stat-label">置换检验 p</span>
          <span class="stat-value">{{ formatNum(startResult.analysis.structure.permutationP) }}</span>
        </div>
        <div class="stat-cell">
          <span class="stat-label">趋势斜率</span>
          <span class="stat-value">{{ formatNum(startResult.analysis.structure.trendSlope, 5) }}</span>
        </div>
        <div class="stat-cell">
          <span class="stat-label">负收益起点占比</span>
          <span class="stat-value">{{ formatNum(startResult.analysis.negativeRatio) }}</span>
        </div>
        <div class="stat-cell">
          <span class="stat-label">断崖数</span>
          <span class="stat-value">{{ startResult.analysis.structure.cliffCount }}</span>
        </div>
      </div>

      <table class="result-table">
        <thead>
          <tr>
            <th>起点</th>
            <th>年化收益</th>
            <th>总收益</th>
            <th>夏普</th>
            <th>最大回撤</th>
            <th>笔数</th>
          </tr>
        </thead>
        <tbody>
          <tr v-for="(run, idx) in startResult.runs" :key="run.startDate" :class="{ failed: !run.ok }">
            <td>{{ run.startDate }}<span v-if="idx === 0" class="baseline-tag">基准</span></td>
            <template v-if="run.ok">
              <td
                :class="valueClass(run.annualReturn)"
              >{{ formatPct(run.annualReturn) }}</td>
              <td :class="valueClass(run.totalReturn)">{{ formatPct(run.totalReturn) }}</td>
              <td>{{ run.sharp === null ? '-' : run.sharp.toFixed(2) }}</td>
              <td>{{ formatPct(run.maxDrawdown) }}</td>
              <td>{{ run.tradeCount }}</td>
            </template>
            <td v-else colspan="5" class="error-cell">{{ run.error }}</td>
          </tr>
        </tbody>
      </table>
    </div>

    <!-- 参数扰动结果 -->
    <div v-if="paramResult" class="result-card">
      <div class="result-header">
        <h4>参数扰动</h4>
        <span class="verdict-badge" :class="paramResult.analysis.verdict.level">
          {{ verdictLabel(paramResult.analysis.verdict.level) }}
        </span>
      </div>

      <p class="verdict-text">{{ paramResult.analysis.verdict.text }}</p>

      <div class="slippage-bar">
        <i class="fas fa-circle-info"></i>
        <span>
          网格 {{ paramResult.gridSize }} 点（对称，不含基准）· 一次只扰动一个参数 ·
          基准年化 {{ formatPct(paramResult.entries[0]?.baseline.annualReturn ?? null) }} ·
          锁死本金 {{ formatCapital(paramResult.baseCapital) }}
        </span>
      </div>

      <div v-for="entry in paramResult.entries" :key="entry.param.nodeId + ':' + entry.param.key" class="param-block">
        <div class="param-block-header">
          <span class="param-block-title">
            {{ entry.param.nodeLabel }} / {{ entry.param.label }}
            <span class="param-block-meta">基准 {{ entry.param.value }} · 步长 {{ entry.param.step }}</span>
          </span>
          <span class="verdict-badge small" :class="entry.verdict.level">
            {{ entry.verdict.level === 'green' ? '绿' : entry.verdict.level === 'yellow' ? '黄' : '红' }}
          </span>
        </div>
        <p class="param-block-verdict">{{ entry.verdict.text }}</p>
        <table class="result-table compact">
          <thead>
            <tr>
              <th>档位</th>
              <th>参数值</th>
              <th>年化收益</th>
              <th>总收益</th>
              <th>夏普</th>
              <th>最大回撤</th>
              <th>笔数</th>
            </tr>
          </thead>
          <tbody>
            <tr class="baseline">
              <td>基准</td>
              <td>{{ entry.param.value }}</td>
              <td :class="valueClass(entry.baseline.annualReturn)">{{ formatPct(entry.baseline.annualReturn) }}</td>
              <td :class="valueClass(entry.baseline.totalReturn)">{{ formatPct(entry.baseline.totalReturn) }}</td>
              <td>{{ entry.baseline.sharp === null ? '-' : entry.baseline.sharp.toFixed(2) }}</td>
              <td>{{ formatPct(entry.baseline.maxDrawdown) }}</td>
              <td>{{ entry.baseline.tradeCount }}</td>
            </tr>
            <tr v-for="v in entry.variants" :key="v.offset" :class="{ failed: !v.ok }">
              <td>{{ v.offset > 0 ? `+${v.offset}` : v.offset }}</td>
              <td>{{ v.value }}</td>
              <template v-if="v.ok">
                <td :class="valueClass(v.annualReturn)">{{ formatPct(v.annualReturn) }}</td>
                <td :class="valueClass(v.totalReturn)">{{ formatPct(v.totalReturn) }}</td>
                <td>{{ v.sharp === null ? '-' : v.sharp.toFixed(2) }}</td>
                <td>{{ formatPct(v.maxDrawdown) }}</td>
                <td>{{ v.tradeCount }}</td>
              </template>
              <td v-else colspan="5" class="error-cell">{{ v.error }}</td>
            </tr>
          </tbody>
        </table>
        <div class="stat-row tight">
          <div class="stat-cell">
            <span class="stat-label">邻域正收益占比</span>
            <span class="stat-value">{{ formatNum(entry.stats.positiveRatio) }}</span>
          </div>
          <div class="stat-cell">
            <span class="stat-label">基准落差</span>
            <span class="stat-value">{{ formatNum(entry.stats.baselineIsolation) }}</span>
          </div>
          <div class="stat-cell">
            <span class="stat-label">最差档位</span>
            <span class="stat-value">{{ formatPct(entry.stats.worstVariant?.annualReturn ?? null) }}</span>
          </div>
        </div>
      </div>
    </div>

    <!-- 维度卡片 -->
    <div class="dimension-grid">
      <StabilityDimensionCard v-for="dim in dimensions" :key="dim.id" :dimension="dim" />
    </div>
  </div>
</template>

<script setup lang="ts">
import { computed, inject, ref, watch } from 'vue'
import StabilityDimensionCard from './stability/StabilityDimensionCard.vue'
import { STABILITY_DIMENSIONS } from './stability/stabilityDimensions'
import { runCapitalSweep, type CapitalSweepResult } from './stability/capitalSweep'
import { runStartSweep, type StartSweepResult } from './stability/startSweep'
import { runParamSweep, type ParamSweepResult } from './stability/paramSweep'
import { extractPerturbableParams, type PerturbableParam } from './stability/paramSweepStats'
import { fetchCapitalPool } from './stability/stabilityApi'

type GetStrategyGraph = () => Record<string, any> | null

// 基准策略图由 StrategyFactory 构建（已做 convertLabelsToKeys + normalizeCodeParams）
const getStrategyGraph = inject<GetStrategyGraph>('getStrategyGraph', null)
// 用户当前配置的本金，用于选归一化基准档
const strategyCapital = inject<{ value: number }>('strategyCapital', null)

const dimensions = STABILITY_DIMENSIONS

const selectedDimensionIds = ref<string[]>(dimensions.map(d => d.id))
const runInterval = ref(500)
const startStepBars = ref(21)
const startTailGuard = ref(126)
const paramGridSize = ref(5)
const abortFlag = ref(false)
const poolHint = ref('读取中...')

const running = ref(false)
const sweepResult = ref<CapitalSweepResult | null>(null)
const startResult = ref<StartSweepResult | null>(null)
const paramResult = ref<ParamSweepResult | null>(null)
const sweepError = ref('')
const progress = ref({ done: 0, total: 0, label: '' })

/** C 组待扫描的数值型参数，随画布实时变化 */
const perturbableParams = computed<PerturbableParam[]>(() => {
  if (!getStrategyGraph) return []
  try {
    return extractPerturbableParams(getStrategyGraph())
  } catch {
    return []
  }
})

const paramKey = (p: PerturbableParam) => `${p.nodeId}:${p.key}`
const selectedParamKeys = ref<string[]>([])

// 默认全选；参数集变化时补齐新参数、剔除已消失的
watch(
  perturbableParams,
  list => {
    const keys = new Set(list.map(paramKey))
    const kept = selectedParamKeys.value.filter(k => keys.has(k))
    selectedParamKeys.value = [...kept, ...list.map(paramKey).filter(k => !kept.includes(k))]
  },
  { immediate: true }
)

/** 展平所有子检验；维度没有子检验时整个维度作为一个可勾选项 */
const availableSubChecks = computed(() => {
  const items: Array<{
    key: string
    dimensionId: string
    id: string
    label: string
    implemented: boolean
  }> = []
  for (const dim of dimensions) {
    if (dim.subChecks) {
      for (const sub of dim.subChecks) {
        items.push({
          key: `${dim.id}:${sub.id}`,
          dimensionId: dim.id,
          id: sub.id,
          label: `${dim.label} / ${sub.label}`,
          implemented: sub.implemented,
        })
      }
    } else {
      items.push({
        key: dim.id,
        dimensionId: dim.id,
        id: dim.id,
        label: dim.label,
        implemented: false,
      })
    }
  }
  return items
})

const selectedSubCheckKeys = ref<string[]>(
  availableSubChecks.value.filter(s => s.implemented).map(s => s.key)
)

const runnableKeys = computed(() =>
  availableSubChecks.value.filter(s => s.implemented && selectedSubCheckKeys.value.includes(s.key))
)

const hasRunnable = computed(() => availableSubChecks.value.some(s => s.implemented))
const canRun = computed(() => runnableKeys.value.length > 0 && !running.value)

const runHint = computed(() => {
  if (!hasRunnable.value) {
    return '所选扰动均需后端支持，暂无法运行'
  }
  const pending = availableSubChecks.value.filter(s => !s.implemented && s.dimensionId === 'initial')
  const suffix = pending.length ? `；${pending.map(p => p.label).join('、')} 待接入` : ''
  return `已接入：${runnableKeys.value.map(k => k.label).join('、')}${suffix}`
})

const verdictLabel = (level: string) =>
  ({ green: '绿 · 符合预期', yellow: '黄 · 需关注', red: '红 · 失去收益能力', unchecked: '未检验' })[level] ?? level

const classificationLabel = (c: string) =>
  ({ random: '随机（无时间结构）', systematic: '系统性依赖', cliff: '断崖（孤立翻转）', insufficient: '样本不足' })[c] ?? c

const formatCapital = (v: number) => `¥${(v / 10000).toFixed(0)} 万`
const formatPct = (v: number | null) => (v === null ? '-' : `${(v * 100).toFixed(2)}%`)
const formatNum = (v: number | null | undefined, digits = 3) =>
  v === null || v === undefined ? '-' : v.toFixed(digits)
const valueClass = (v: number | null) => (v === null ? '' : v > 0 ? 'pos' : v < 0 ? 'neg' : '')

const ratioOf = (list: Array<{ capital: number; ratio: number | null }>, capital: number) =>
  list.find(item => item.capital === capital)?.ratio ?? null

const isSelected = (key: string) => selectedSubCheckKeys.value.includes(key)

async function handleRun() {
  if (!canRun.value) return

  if (!getStrategyGraph) {
    sweepError.value = '未获取到基准策略图'
    return
  }
  const baseGraph = getStrategyGraph()
  if (!baseGraph) {
    sweepError.value = '当前画布没有已保存的策略，无法作为扫描基准'
    sweepResult.value = null
    startResult.value = null
    paramResult.value = null
    return
  }

  running.value = true
  abortFlag.value = false
  sweepError.value = ''
  sweepResult.value = null
  startResult.value = null
  paramResult.value = null
  progress.value = { done: 0, total: 0, label: '' }

  const configuredCapital = strategyCapital?.value ?? baseGraph.capital ?? 1_000_000

  try {
    if (isSelected('initial:capital')) {
      const capitalResult = await runCapitalSweep({
        baseGraph,
        configuredCapital,
        runInterval: runInterval.value,
        onProgress: (done, total) => {
          progress.value = { done, total, label: '资金规模' }
        },
        shouldAbort: () => abortFlag.value,
      })
      sweepResult.value = capitalResult
    }

    if (isSelected('initial:startShift')) {
      const startSweepResult = await runStartSweep({
        baseGraph,
        baseCapital: configuredCapital,
        stepBars: Math.max(1, startStepBars.value),
        tailGuardBars: Math.max(0, startTailGuard.value),
        runInterval: runInterval.value,
        onProgress: (done, total, startDate) => {
          progress.value = { done, total, label: `起点 ${startDate}` }
        },
        shouldAbort: () => abortFlag.value,
      })
      startResult.value = startSweepResult
    }

    if (isSelected('param:grid')) {
      const chosen = perturbableParams.value.filter(p => selectedParamKeys.value.includes(paramKey(p)))
      if (!chosen.length) {
        throw new Error('未勾选任何待扫描参数')
      }
      const paramSweepResult = await runParamSweep({
        baseGraph,
        baseCapital: configuredCapital,
        selectedParams: chosen,
        gridSize: Math.max(3, paramGridSize.value),
        runInterval: runInterval.value,
        onProgress: (done, total, label) => {
          progress.value = { done, total, label }
        },
        shouldAbort: () => abortFlag.value,
      })
      paramResult.value = paramSweepResult
    }
  } catch (err: any) {
    sweepError.value = err?.response?.data?.message || err?.message || String(err)
  } finally {
    running.value = false
  }
}

fetchCapitalPool()
  .then(pool => {
    poolHint.value = `资金池可用 ¥${(pool.totalAvailable / 10000).toFixed(0)} 万（总初始 ¥${(pool.initialCapital / 10000).toFixed(0)} 万，已分配 ¥${(pool.totalAllocated / 10000).toFixed(0)} 万）`
  })
  .catch(err => {
    poolHint.value = `读取失败：${err?.message || err}`
  })
</script>

<style scoped lang="scss">
.stability-panel {
  flex: 1;
  display: flex;
  flex-direction: column;
  gap: 16px;
  width: 100%;
  min-height: 0;
  overflow-y: auto;
  padding: 16px 20px 24px;
}

.config-card,
.result-card {
  background: var(--panel-bg);
  border: 1px solid var(--border);
  border-radius: 10px;
  box-shadow: 0 4px 12px rgba(0, 0, 0, 0.15);
  flex-shrink: 0;
}

.config-header,
.result-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 14px 20px;
  border-bottom: 1px solid var(--border);

  h3,
  h4 {
    margin: 0;
    font-size: 15px;
    font-weight: 600;
    color: var(--text);
    display: flex;
    align-items: center;
    gap: 8px;

    i {
      color: var(--primary);
    }
  }
}

.pending-tag {
  font-size: 11px;
  padding: 3px 10px;
  border-radius: 10px;
  color: var(--accent);
  background: color-mix(in srgb, var(--accent) 10%, transparent);
  border: 1px solid color-mix(in srgb, var(--accent) 40%, transparent);
  display: flex;
  align-items: center;
  gap: 6px;

  &.live {
    color: var(--secondary);
    background: color-mix(in srgb, var(--secondary) 10%, transparent);
    border-color: color-mix(in srgb, var(--secondary) 40%, transparent);
  }
}

.verdict-badge {
  font-size: 12px;
  padding: 4px 12px;
  border-radius: 10px;
  border: 1px solid var(--border);
  color: var(--text-secondary);

  &.green {
    color: var(--secondary);
    border-color: color-mix(in srgb, var(--secondary) 50%, transparent);
    background: color-mix(in srgb, var(--secondary) 12%, transparent);
  }

  &.yellow {
    color: var(--accent);
    border-color: color-mix(in srgb, var(--accent) 50%, transparent);
    background: color-mix(in srgb, var(--accent) 12%, transparent);
  }

  &.red {
    color: #f44336;
    border-color: rgba(244, 67, 54, 0.5);
    background: rgba(244, 67, 54, 0.12);
  }
}

.config-body {
  padding: 16px 20px 20px;
  display: flex;
  flex-direction: column;
  gap: 16px;
}

.field > label {
  display: block;
  font-size: 12px;
  color: var(--text-secondary);
  margin-bottom: 8px;
}

.dimension-checks,
.subcheck-list {
  display: flex;
  flex-wrap: wrap;
  gap: 16px;
}

.check-item {
  display: flex;
  align-items: center;
  gap: 8px;
  cursor: pointer;
  user-select: none;

  &.disabled {
    opacity: 0.5;
    cursor: not-allowed;
  }

  input[type='checkbox'] {
    width: 16px;
    height: 16px;
    cursor: pointer;
    accent-color: var(--primary);
  }
}

.check-text {
  font-size: 13px;
  color: var(--text);
}

.impl-tag {
  font-size: 10px;
  padding: 2px 7px;
  border-radius: 8px;
  border: 1px solid color-mix(in srgb, var(--accent) 45%, transparent);
  color: var(--accent);

  &.on {
    border-color: color-mix(in srgb, var(--secondary) 45%, transparent);
    color: var(--secondary);
  }
}

.config-inline {
  display: flex;
  flex-wrap: wrap;
  gap: 20px;
  align-items: flex-end;
}

.inline-field {
  width: 140px;

  &.wide {
    width: auto;
    flex: 1;
  }

  > label {
    display: block;
    font-size: 12px;
    color: var(--text-secondary);
    margin-bottom: 6px;
  }
}

.pool-hint {
  font-size: 12px;
  color: var(--text-secondary);
  padding: 6px 0;
}

.num-input {
  width: 100%;
  padding: 6px 10px;
  background: var(--panel-bg);
  border: 1px solid var(--border);
  border-radius: 6px;
  color: var(--text);
  font-size: 13px;
  font-family: inherit;

  &:focus {
    outline: none;
    border-color: var(--primary);
  }
}

.config-actions {
  display: flex;
  align-items: center;
  gap: 14px;
  padding-top: 14px;
  border-top: 1px solid var(--border);
}

.btn {
  padding: 8px 20px;
  border-radius: 6px;
  font-size: 14px;
  font-weight: 500;
  font-family: inherit;
  cursor: pointer;
  border: none;
  display: flex;
  align-items: center;
  gap: 8px;
  transition: all 0.2s;

  &:disabled {
    opacity: 0.5;
    cursor: not-allowed;
  }
}

.btn-primary {
  background: var(--primary);
  color: #fff;
}

.btn-ghost {
  background: transparent;
  border: 1px solid var(--border);
  color: var(--text-secondary);
}

.run-hint {
  font-size: 12px;
  color: var(--text-secondary);
}

/* 结果区 */

.verdict-text {
  margin: 0;
  padding: 14px 20px 0;
  font-size: 13px;
  line-height: 1.7;
  color: var(--text);
}

.slippage-bar {
  display: flex;
  align-items: flex-start;
  gap: 8px;
  margin: 12px 20px 0;
  padding: 8px 10px;
  border-radius: 6px;
  font-size: 12px;
  line-height: 1.6;
  color: var(--text-secondary);
  background: var(--darker-bg);
  border: 1px solid var(--border);

  &.off {
    color: var(--accent);
    border-color: color-mix(in srgb, var(--accent) 30%, transparent);
    background: color-mix(in srgb, var(--accent) 8%, transparent);
  }

  i {
    margin-top: 2px;
    flex-shrink: 0;
  }
}

.result-table {
  width: calc(100% - 40px);
  margin: 14px 20px 0;
  border-collapse: collapse;
  font-size: 12px;

  th,
  td {
    padding: 7px 10px;
    text-align: right;
    border-bottom: 1px solid var(--border);
    white-space: nowrap;
  }

  th {
    color: var(--text-secondary);
    font-weight: 500;
    background: var(--darker-bg);
  }

  td {
    color: var(--text);
    font-variant-numeric: tabular-nums;
  }

  th:first-child,
  td:first-child {
    text-align: left;
  }

  tr.baseline td {
    background: rgba(41, 98, 255, 0.08);
  }

  tr.failed td {
    color: #f44336;
  }

  .pos {
    color: var(--secondary);
  }

  .neg {
    color: #f44336;
  }

  .error-cell {
    text-align: left;
    color: #f44336;
    white-space: normal;
  }
}

.epsilon-line {
  margin: 12px 20px 16px;
  font-size: 12px;
  color: var(--text-secondary);
}

.stat-row {
  display: flex;
  flex-wrap: wrap;
  gap: 10px;
  margin: 12px 20px 0;
}

.stat-cell {
  display: flex;
  flex-direction: column;
  gap: 3px;
  padding: 8px 12px;
  min-width: 110px;
  border-radius: 6px;
  background: var(--darker-bg);
  border: 1px solid var(--border);
}

.stat-label {
  font-size: 11px;
  color: var(--text-secondary);
}

.stat-value {
  font-size: 14px;
  color: var(--text);
  font-variant-numeric: tabular-nums;
}

.baseline-tag {
  margin-left: 6px;
  font-size: 10px;
  padding: 1px 6px;
  border-radius: 6px;
  background: color-mix(in srgb, var(--primary) 20%, transparent);
  color: var(--primary);
}

/* 参数扰动 */

.label-hint {
  margin-left: 8px;
  font-weight: 400;
  opacity: 0.75;
}

.param-checks {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(280px, 1fr));
  gap: 8px 16px;
}

.param-meta {
  margin-left: 8px;
  font-size: 11px;
  color: var(--text-secondary);
  font-family: 'Consolas', 'Courier New', monospace;
}

.no-params {
  font-size: 13px;
  color: var(--text-secondary);
  padding: 8px 0;
}

.param-block {
  margin: 16px 20px 0;
  padding: 14px;
  border-radius: 8px;
  background: var(--darker-bg);
  border: 1px solid var(--border);
}

.param-block-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 12px;
}

.param-block-title {
  font-size: 13px;
  font-weight: 600;
  color: var(--text);
}

.param-block-meta {
  margin-left: 8px;
  font-size: 11px;
  font-weight: 400;
  color: var(--text-secondary);
  font-family: 'Consolas', 'Courier New', monospace;
}

.param-block-verdict {
  margin: 8px 0 0;
  font-size: 12px;
  line-height: 1.6;
  color: var(--text-secondary);
}

.verdict-badge.small {
  font-size: 11px;
  padding: 2px 10px;
}

.result-table.compact {
  width: 100%;
  margin: 10px 0 0;

  th,
  td {
    padding: 5px 8px;
    font-size: 11px;
  }
}

.stat-row.tight {
  margin: 10px 0 0;
}

.result-card.error .verdict-text {
  color: #f44336;
  padding-bottom: 16px;
}

.dimension-grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(420px, 1fr));
  gap: 16px;
  align-items: start;
}
</style>
