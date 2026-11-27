<template>
  <Teleport :to="overlayHost ?? undefined" :disabled="!maximized || !overlayHost">
    <div class="ui-frame" :class="{ 'is-maximized': maximized }">
      <!-- 头部：标题 + 右上最大化 -->
      <div class="ui-frame__head">
        <span class="ui-frame__icon"><i :class="iconClass"></i></span>
        <span class="ui-frame__title">{{ spec.title }}</span>
        <span v-if="spec.subtitle" class="ui-frame__subtitle">{{ spec.subtitle }}</span>
        <span class="grow"></span>

        <span v-if="resolving" class="ui-frame__busy">
          <i class="fas fa-spinner fa-spin"></i> 加载中
        </span>
        <span v-if="resolveError" class="ui-frame__error" :data-tooltip="resolveError">
          <i class="fas fa-circle-exclamation"></i>
        </span>

        <button
          v-if="supportsDensity"
          class="icon-btn"
          :aria-label="densityRef === 'compact' ? '放大产物' : '紧凑显示'"
          :data-tooltip="densityRef === 'compact' ? '放大产物' : '紧凑显示'"
          @click="toggleDensity"
        >
          <i :class="densityRef === 'compact' ? 'fas fa-expand' : 'fas fa-compress'"></i>
        </button>

        <button
          v-if="canExportPng"
          class="icon-btn"
          aria-label="导出 PNG"
          data-tooltip="导出 PNG"
          @click="onExportPng"
        >
          <i class="fas fa-image"></i>
        </button>

        <button
          v-if="canExportCsv"
          class="icon-btn"
          aria-label="导出 CSV"
          data-tooltip="导出 CSV"
          @click="onExportCsv"
        >
          <i class="fas fa-file-csv"></i>
        </button>

        <button
          class="icon-btn"
          :aria-label="maximized ? '退出放大' : '最大化'"
          :data-tooltip="maximized ? '退出放大' : '最大化'"
          @click="toggleMaximize"
        >
          <i :class="maximized ? 'fas fa-compress' : 'fas fa-expand'"></i>
        </button>
      </div>

      <!-- 主体：按 type 分发到视图组件 -->
      <div ref="bodyRef" class="ui-frame__body">
        <component
          :is="viewComponent"
          v-if="viewComponent"
          :data="data"
          :params="params"
          :height="effectiveHeight"
          :density="densityRef"
        />
        <div v-else class="ui-frame__fallback">
          未登记的 UI 类型「{{ spec.type }}」，请检查 UI 类型注册表。
        </div>
      </div>

      <!-- 底部：参数控件条（搭在图表下方）+ 产物动作 -->
      <div v-if="controls.length || actions.length || resultMessage" class="ui-frame__foot">
        <UiControls
          v-if="controls.length"
          :controls="controls"
          :params="params"
          :dirty="dirty"
          @change="onControlChange"
          @reset="resetParams"
        />

        <span v-if="resultMessage" class="ui-frame__result">{{ resultMessage }}</span>

        <button
          v-for="action in actions"
          :key="action.id"
          class="action-btn"
          :class="{ 'is-primary': action.kind === 'apply_patch' }"
          :disabled="actionState[action.id] === 'pending' || actionState[action.id] === 'done' || action.disabled"
          :data-tooltip="action.disabled ? action.disabledReason : undefined"
          @click="execute(action)"
        >
          <i :class="actionIcon(action)"></i>
          {{ actionLabel(action) }}
        </button>
      </div>
    </div>
  </Teleport>
</template>

<script setup lang="ts">
import { computed, inject, onMounted, onUnmounted, ref, watch } from 'vue'
import { getUiType, mergeParams } from '@/lib/charts/registry'
import sseService from '@/ts/SSEService'
import { isTerminalEvent, matchesBinding, normalizeStatusPayload } from '@/lib/ui/protocol'
import type { UiBinding, UiReducer } from '@/lib/ui/protocol'
import { applyAuthHeader } from '@/lib/tools/common/auth'
import { getResolver } from '@/lib/charts/resolvers'
import { exportCsv, exportPng } from '@/lib/charts/export'
import { useChatStore } from '@/stores/chatStore'
import type { UiControlDef, UiSpec } from '@/lib/charts/types'
import type { ArtifactAction, ArtifactDensity, ArtifactEnvelope } from '@/lib/agent/artifacts'
import { useArtifactActions } from '../useArtifactActions'
import { UI_OVERLAY_KEY } from './overlayContext'
import UiControls from './UiControls.vue'

const props = withDefaults(
  defineProps<{
    spec: UiSpec
    artifact?: ArtifactEnvelope
    density?: ArtifactDensity
    height?: number
    /** 所属消息 id —— 产物里的动作产出的新产物要挂回同一条消息 */
    messageId?: string
  }>(),
  { density: 'full' },
)

const overlay = inject(UI_OVERLAY_KEY, null)
const overlayHost = computed(() => overlay?.host?.value ?? null)
const chatStore = useChatStore()

/* ---------- 本地状态 ---------- */

const maximized = ref(false)

const typeDef = computed(() => getUiType(props.spec.type))
const viewComponent = computed(() => typeDef.value?.component ?? null)

/** 当前参数：初始 = 类型默认值 + 产物带的 params */
const params = ref<Record<string, unknown>>(
  mergeParams(typeDef.value, props.spec.params),
)

/** 当前数据：初始 = 产物里的快照 */
const data = ref<Record<string, unknown>>(props.spec.data ?? {})

/** 控件定义：类型静态定义，可被产物自带或 resolver 回传覆盖 */
const staticControls = computed<UiControlDef[]>(() => typeDef.value?.controls ?? [])
const resolverControls = ref<UiControlDef[] | null>(props.spec.controls ?? null)
const controls = computed<UiControlDef[]>(
  () => resolverControls.value ?? staticControls.value,
)

const resolving = ref(false)
const resolveError = ref('')

/* ---------- 高度与密度 ---------- */

/** 密度：由本组件自己管（外壳自带切换按钮） */
const densityRef = ref<ArtifactDensity>(props.density ?? 'full')

/** 是否支持双密度 */
const supportsDensity = computed(() => typeDef.value?.compact === true)

function toggleDensity() {
  densityRef.value = densityRef.value === 'compact' ? 'full' : 'compact'
}

const effectiveHeight = computed(() => {
  if (maximized.value) {
    // 放大态：图区占满宿主（Agent 面板）减去头尾
    const hostHeight = overlayHost.value?.clientHeight ?? 0
    const base = hostHeight || Math.floor(window.innerHeight * 0.55)
    return Math.max(320, base - 110)
  }
  const hints = props.artifact?.ui
  if (densityRef.value === 'compact') {
    return hints?.compactHeight ?? (hints?.height ? Math.round(hints.height * 0.55) : 165)
  }
  return props.height ?? hints?.height ?? 300
})

/* ---------- 实时绑定（inbound） ---------- */

/**
 * 按 spec.binding 订阅服务端事件，用类型定义的 reducer 合并进 data。
 *
 * 先 replay 再 subscribe：POST 返回 → 工具造产物 → 事件流渲染 → 组件挂载，
 * started/progress 大概率已经发过了，不回放的话卡片会停在初始态。
 */
let unsubscribeBinding: (() => void) | null = null
let bindingTimer: ReturnType<typeof setTimeout> | null = null
/** 回放阶段就命中终态时置位，避免回放后还去订阅（订阅了也没人再退订） */
let bindingStopped = false
/** 轮询定时器 */
let pollTimer: ReturnType<typeof setTimeout> | null = null
/** 轮询批次号：换了产物后旧请求回来就丢弃 */
let pollGeneration = 0

function stopBinding() {
  bindingStopped = true
  unsubscribeBinding?.()
  unsubscribeBinding = null
  if (bindingTimer) {
    clearTimeout(bindingTimer)
    bindingTimer = null
  }
  if (pollTimer) {
    clearTimeout(pollTimer)
    pollTimer = null
  }
}

/** 终态写回产物：reducer 只改本地 ref，不写回的话重载页面卡片又从 running 复活 */
function persistData() {
  const id = props.artifact?.id
  if (!id) return
  chatStore.updateArtifactData(id, data.value)
}

function startBinding() {
  stopBinding()
  bindingStopped = false

  const binding = props.spec.binding
  const reducer = typeDef.value?.reducer
  if (!binding || !reducer) return

  if (binding.kind === 'poll') {
    void startPolling(binding, reducer)
    return
  }
  if (binding.kind !== 'sse') return

  const apply = (payload: unknown) => {
    if (!payload || typeof payload !== 'object') return
    const event = payload as Record<string, unknown>
    if (!matchesBinding(binding, event)) return

    data.value = reducer(data.value, event)
    persistData()

    // 两种终态信号都认：事件自带 status 字段（stopOn），或 reducer 把 data.status
    // 收敛到 done/failed（回测这类没有 status 字段的链路，只能从文案里认）
    const status = (data.value as { status?: unknown }).status
    if (isTerminalEvent(binding, event) || status === 'done' || status === 'failed') {
      stopBinding()
    }
  }

  sseService.replay(binding.event, apply)
  if (bindingStopped) return
  unsubscribeBinding = sseService.onEvent(binding.event, apply)

  const timeoutMs = binding.timeoutMs
  if (timeoutMs && timeoutMs > 0) armWatchdog(timeoutMs, binding, apply)
}

/* ---------- 轮询绑定（poll） ---------- */

const POLL_DEFAULT_INTERVAL = 3000
const POLL_REQUEST_TIMEOUT = 10_000

/** 轮询服务端状态接口；载荷归一后走同一个 reducer */
async function startPolling(
  binding: Extract<UiBinding, { kind: 'poll' }>,
  reducer: UiReducer,
): Promise<void> {
  const intervalMs = Math.max(1000, binding.intervalMs ?? POLL_DEFAULT_INTERVAL)
  const deadline = Date.now() + binding.timeoutMs
  const generation = ++pollGeneration

  const apply = (payload: Record<string, unknown>) => {
    data.value = reducer(data.value, payload)
    persistData()
    const status = (data.value as { status?: unknown }).status
    // 后端终态有三种：done / failed / error
    if (status === 'done' || status === 'failed' || status === 'error') stopBinding()
  }

  const tick = async () => {
    if (bindingStopped || generation !== pollGeneration) return
    try {
      const axios = (await import('axios')).default
      applyAuthHeader(axios)
      const res = await axios.get(binding.url, { timeout: POLL_REQUEST_TIMEOUT })
      if (bindingStopped || generation !== pollGeneration) return
      apply(normalizeStatusPayload((res.data ?? {}) as Record<string, unknown>))
    } catch (e: any) {
      // 404：服务端没有这个任务了，再问也没用
      if (e?.response?.status === 404) {
        data.value = {
          ...data.value,
          status: 'failed',
          detail: '服务端已找不到该任务（可能已过期或被丢弃）',
        }
        persistData()
        stopBinding()
        return
      }
      // 其它错误（网络抖动、服务重启）继续轮询，交给总超时兜底
      console.warn('[UI Poll] 轮询失败:', e)
    }
    if (bindingStopped || generation !== pollGeneration) return
    if (Date.now() >= deadline) {
      data.value = {
        ...data.value,
        status: 'failed',
        detail: `轮询超时（${Math.round(binding.timeoutMs / 1000)} 秒）仍未拿到终态`,
      }
      persistData()
      stopBinding()
      return
    }
    pollTimer = setTimeout(() => {
      void tick()
    }, intervalMs)
  }

  await tick()
}

/**
 * 看门狗：超时没等到终态，先问服务端这任务到底成没成，再决定判不判失败。
 *
 * 推送是尽力而为 —— 客户端没连上、后端重启、事件发得太早，终态事件就收不到。
 * 直接判失败会把「服务端已经导完 4090 行」显示成失败；先查一次状态就能纠正。
 */
const WATCHDOG_MAX_ROUNDS = 3

function armWatchdog(
  timeoutMs: number,
  binding: Extract<UiBinding, { kind: 'sse' }>,
  apply: (payload: unknown) => void,
  round = 0,
): void {
  bindingTimer = setTimeout(() => {
    void onWatchdog(timeoutMs, binding, apply, round)
  }, timeoutMs)
}

async function onWatchdog(
  timeoutMs: number,
  binding: Extract<UiBinding, { kind: 'sse' }>,
  apply: (payload: unknown) => void,
  round: number,
): Promise<void> {
  const status = (data.value as { status?: unknown }).status
  if (status === 'done' || status === 'failed') return

  if (binding.statusUrl) {
    try {
      const axios = (await import('axios')).default
      applyAuthHeader(axios)
      const res = await axios.get(binding.statusUrl)
      const payload = (res.data ?? {}) as Record<string, unknown>
      const remote = String(payload.status ?? '')
      if (remote === 'done' || remote === 'failed' || remote === 'error') {
        // 服务端说完了 —— 以它为准，别把已完成的任务显示成失败
        const progress = payload.progress
        apply(
          progress && typeof progress === 'object'
            ? { ...payload, ...(progress as Record<string, unknown>) }
            : payload,
        )
        return
      }
      if (remote === 'running' && round + 1 < WATCHDOG_MAX_ROUNDS) {
        // 还在跑：再等一轮，别急着判失败
        armWatchdog(timeoutMs, binding, apply, round + 1)
        return
      }
      if (remote === 'running') {
        data.value = {
          ...data.value,
          status: 'failed',
          detail: `服务端任务仍在进行（已等 ${Math.round((timeoutMs * WATCHDOG_MAX_ROUNDS) / 1000)} 秒），事件连接可能已断开`,
        }
        persistData()
        stopBinding()
        return
      }
    } catch {
      // 查不到就按失败处理 —— 卡片不能一直转圈
    }
  }

  data.value = {
    ...data.value,
    status: 'failed',
    detail: '未收到服务端完成事件（任务可能已被服务端丢弃，或事件连接已断开）',
  }
  persistData()
  stopBinding()
}

onMounted(startBinding)
onUnmounted(stopBinding)

/* ---------- 导出 ---------- */

const bodyRef = ref<HTMLElement | null>(null)

/** 是否能导出 PNG（图表类才有 ECharts 实例） */
const canExportPng = computed(
  () => typeDef.value?.exportable === true && ['timeseries', 'histogram', 'matrix'].includes(props.spec.type),
)

/** 是否能导出 CSV */
const canExportCsv = computed(() => typeDef.value?.exportable === true)

function onExportPng() {
  const canvas = bodyRef.value?.querySelector('.ui-view__canvas') as HTMLElement | null
  const ok = exportPng(canvas, props.spec)
  if (!ok) {
    resolveError.value = '没有找到可导出的图表实例'
  }
}

function onExportCsv() {
  exportCsv(props.spec, data.value)
}

/* ---------- 产物动作 ---------- */

const artifactRef = computed(() => props.artifact)
const messageIdRef = computed(() => props.messageId)
const { actionState, resultMessage, actions, execute } = useArtifactActions(
  artifactRef,
  undefined,
  messageIdRef,
)

/* ---------- 参数变化 ---------- */

/** 参数是否已偏离默认 */
const dirty = computed(() => {
  const base = mergeParams(typeDef.value, undefined)
  return Object.keys(params.value).some((k) => params.value[k] !== base[k])
})

async function onControlChange(payload: { key: string; value: unknown }) {
  params.value = { ...params.value, [payload.key]: payload.value }

  const ctrl = controls.value.find((c) => c.key === payload.key)
  // 呈现层参数：视图自己重算即可
  if (!ctrl || ctrl.layer !== 'data') return

  // 数据层参数：回源换 data
  await resolveData()
}

/** 调用 resolver 回源 */
async function resolveData() {
  const resolverId = typeDef.value?.resolver
  if (!resolverId) return
  const resolver = getResolver(resolverId)
  if (!resolver) {
    resolveError.value = `未注册的 resolver：${resolverId}`
    return
  }

  resolving.value = true
  resolveError.value = ''
  try {
    const out = await resolver(params.value)
    data.value = out.data
    if (out.controls) resolverControls.value = out.controls
  } catch (e) {
    resolveError.value = e instanceof Error ? e.message : String(e)
  } finally {
    resolving.value = false
  }
}

function resetParams() {
  params.value = mergeParams(typeDef.value, props.spec.params)
  data.value = props.spec.data ?? {}
  resolverControls.value = props.spec.controls ?? null
  resolveError.value = ''
}

/* ---------- 最大化 ---------- */

function toggleMaximize() {
  maximized.value = !maximized.value
}

function onKeydown(e: KeyboardEvent) {
  if (e.key === 'Escape' && maximized.value) {
    maximized.value = false
  }
}

watch(
  maximized,
  (on) => {
    if (on) {
      window.addEventListener('keydown', onKeydown)
    } else {
      window.removeEventListener('keydown', onKeydown)
    }
  },
)

onUnmounted(() => {
  window.removeEventListener('keydown', onKeydown)
})

/* ---------- 产物切换时重置 ---------- */

watch(
  () => props.spec,
  () => {
    params.value = mergeParams(typeDef.value, props.spec.params)
    data.value = props.spec.data ?? {}
    resolverControls.value = props.spec.controls ?? null
    resolveError.value = ''
    maximized.value = false
    densityRef.value = props.density ?? 'full'
    startBinding()
  },
)

/* ---------- 图标 ---------- */

const iconClass = computed(() => {
  const map: Record<string, string> = {
    timeseries: 'fas fa-chart-line',
    histogram: 'fas fa-chart-bar',
    matrix: 'fas fa-table-cells',
    table: 'fas fa-table',
    metric: 'fas fa-gauge-high',
    progress: 'fas fa-bars-progress',
  }
  return map[props.spec.type] ?? 'fas fa-cube'
})

function actionIcon(action: ArtifactAction): string {
  const state = actionState[action.id]
  if (state === 'pending') return 'fas fa-spinner fa-spin'
  if (state === 'done') return 'fas fa-check'
  switch (action.kind) {
    case 'apply_patch':
      return 'fas fa-check'
    case 'run_backtest':
    case 'run_strategy':
      return 'fas fa-play'
    case 'stop_strategy':
      return 'fas fa-stop'
    case 'deploy_strategy':
      return 'fas fa-upload'
    case 'open_panel':
      return 'fas fa-table-columns'
    case 'data_manager.delete_quote':
    case 'data_manager.delete_finance':
    case 'strategy.delete_strategy':
      return 'fas fa-trash'
    default:
      return 'fas fa-bolt'
  }
}

function actionLabel(action: ArtifactAction): string {
  const state = actionState[action.id]
  if (state === 'pending') return action.pendingLabel || '执行中…'
  if (state === 'done') return action.doneLabel || '已完成'
  return action.label
}
</script>

<style scoped>
.ui-frame {
  border: 1px solid var(--border, #2a3449);
  border-radius: 10px;
  background: var(--panel-bg, #1a2236);
  overflow: hidden;
}

.ui-frame.is-maximized {
  position: absolute;
  inset: 0;
  z-index: 40;
  border-radius: 0;
  border: none;
  display: flex;
  flex-direction: column;
  pointer-events: auto;
}

.ui-frame.is-maximized .ui-frame__body {
  flex: 1;
  overflow: auto;
}

.ui-frame__head {
  display: flex;
  align-items: center;
  gap: 9px;
  padding: 10px 13px;
  border-bottom: 1px solid rgba(74, 85, 104, 0.28);
}

.ui-frame__icon {
  color: var(--primary, #2962ff);
  display: inline-flex;
  font-size: 12px;
}

.ui-frame__title {
  font-size: 13px;
  font-weight: 500;
  color: var(--text, #e0e0e0);
}

.ui-frame__subtitle {
  font-size: 11.5px;
  color: #6f7b8a;
}

.grow {
  flex: 1;
}

.ui-frame__busy {
  font-size: 11px;
  color: #6f7b8a;
  display: inline-flex;
  align-items: center;
  gap: 5px;
}

.ui-frame__error {
  font-size: 12px;
  color: #ef5350;
}

.icon-btn {
  width: 22px;
  height: 22px;
  border-radius: 5px;
  border: 1px solid transparent;
  background: transparent;
  color: #6f7b8a;
  cursor: pointer;
  font-size: 10px;
}

.icon-btn:hover {
  color: #e0e0e0;
  border-color: var(--border, #2a3449);
}

.ui-frame__body {
  padding: 4px 0;
}

.ui-frame__fallback {
  padding: 20px 13px;
  font-size: 12px;
  color: #6f7b8a;
}

.ui-frame__foot {
  display: flex;
  flex-wrap: wrap;
  align-items: center;
  gap: 8px;
  border-top: 1px solid rgba(74, 85, 104, 0.22);
}

.ui-frame__result {
  font-size: 11px;
  color: #a0aec0;
  padding: 0 13px;
}

.action-btn {
  height: 23px;
  margin: 0 13px 0 0;
  padding: 0 9px;
  font-size: 11px;
  border-radius: 5px;
  border: 1px solid var(--border, #2a3449);
  background: transparent;
  color: #a0aec0;
  cursor: pointer;
  display: inline-flex;
  align-items: center;
  gap: 5px;
  font-family: inherit;
  white-space: nowrap;
}

.action-btn:hover:not(:disabled) {
  color: #e0e0e0;
  border-color: var(--primary, #2962ff);
}

.action-btn.is-primary {
  background: var(--primary, #2962ff);
  border-color: var(--primary, #2962ff);
  color: #fff;
}

.action-btn:disabled {
  opacity: 0.5;
  cursor: default;
}
</style>
