<template>
  <!-- UI 产物：UiFrame 自带标题 / 最大化 / 参数条，不套通用卡片外壳 -->
  <component
    :is="renderer"
    v-if="isUiKind && renderer"
    v-bind="rendererProps"
  />

  <div v-else class="artifact-card">
    <div class="artifact-head">
      <span class="artifact-icon"><i :class="iconClass"></i></span>
      <span class="artifact-title">{{ artifact.title }}</span>
      <span v-if="artifact.subtitle" class="artifact-subtitle">{{ artifact.subtitle }}</span>
      <span class="spacer"></span>

      <!-- 密度切换：完整态给「紧凑」，紧凑态给醒目的「放大」 -->
      <button
        v-if="supportsDensity && density === 'full'"
        class="icon-btn"
        aria-label="紧凑显示"
        data-tooltip="紧凑显示"
        @click="toggleDensity"
      >
        <i class="fas fa-compress"></i>
      </button>
      <button
        v-if="supportsDensity && density === 'compact'"
        class="enlarge-btn"
        @click="toggleDensity"
      >
        <i class="fas fa-expand"></i>
        放大
      </button>

      <button
        v-if="canCollapse"
        class="icon-btn"
        :aria-label="collapsed ? '展开产物' : '收起产物'"
        :data-tooltip="collapsed ? '展开产物' : '收起产物'"
        @click="collapsed = !collapsed"
      >
        <i :class="collapsed ? 'fas fa-chevron-down' : 'fas fa-chevron-up'"></i>
      </button>
    </div>

    <div v-show="!collapsed" class="artifact-body">
      <component
        :is="renderer"
        v-if="renderer"
        v-bind="rendererProps"
      />
      <AgentFallbackArtifact
        v-else
        :payload="artifact.payload"
        :kind="artifact.kind"
        :artifact="artifact"
      />
    </div>

    <div v-if="actions.length || artifact.source || resultMessage" class="artifact-foot">
      <span class="artifact-source">
        <template v-if="resultMessage">{{ resultMessage }}</template>
        <template v-else-if="artifact.source">{{ artifact.source }}</template>
      </span>
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
</template>

<script setup lang="ts">
import { computed, ref, watch } from 'vue'
import { getArtifactSpec } from '@/lib/agent/artifactRegistry'
import type { ArtifactAction, ArtifactDensity, ArtifactEnvelope } from '@/lib/agent/artifacts'
import AgentFallbackArtifact from './AgentFallbackArtifact.vue'
import { useArtifactActions } from './useArtifactActions'

const props = defineProps<{ artifact: ArtifactEnvelope }>()

const applied = ref(false)

const spec = computed(() => getArtifactSpec(props.artifact.kind))
const renderer = computed(() => spec.value?.render ?? null)

/** 折叠态：初始值由产物的 ui.defaultCollapsed 决定 */
const collapsed = ref(props.artifact.ui?.defaultCollapsed ?? false)

/** 密度态：紧凑 ↔ 完整 */
const density = ref<ArtifactDensity>(spec.value?.defaultDensity ?? 'full')

/** 产物切换时重置折叠与密度 */
watch(
  () => props.artifact.id,
  () => {
    collapsed.value = props.artifact.ui?.defaultCollapsed ?? false
    density.value = spec.value?.defaultDensity ?? 'full'
  },
)

/** 是否允许折叠（产物自身的 ui 优先，其次注册表） */
const canCollapse = computed(
  () => !!renderer.value && (props.artifact.ui?.collapsible ?? spec.value?.collapsible ?? true),
)

/** 是否支持双密度 */
const supportsDensity = computed(() => !!renderer.value && spec.value?.compact === true)

function toggleDensity() {
  density.value = density.value === 'compact' ? 'full' : 'compact'
}

/** 有效渲染高度：紧凑态用 compactHeight，完整态用 height */
const effectiveHeight = computed<number | undefined>(() => {
  const ui = props.artifact.ui
  if (density.value === 'compact') {
    return ui?.compactHeight ?? (ui?.height ? Math.round(ui.height * 0.55) : 160)
  }
  return ui?.height
})

/** UI 产物走 UiFrame（自带外壳），其余走通用卡片 */
const isUiKind = computed(() => props.artifact.kind === 'ui')

/** 只传渲染器真正需要的 props，避免多余属性落到 DOM 上 */
const rendererProps = computed<Record<string, unknown>>(() => {
  if (!spec.value) return {}

  if (isUiKind.value) {
    // UiFrame 的 props 是 spec / artifact / height / density
    return {
      spec: props.artifact.payload,
      artifact: props.artifact,
      height: effectiveHeight.value,
      density: density.value,
    }
  }

  const out: Record<string, unknown> = { payload: props.artifact.payload }
  const height = effectiveHeight.value
  if (height) out.height = height
  if (supportsDensity.value) out.density = density.value
  if (props.artifact.kind === 'patch') out.applied = applied.value
  return out
})

const iconClass = computed(() => {
  const map: Record<string, string> = {
    chart: 'fas fa-chart-line',
    metric: 'fas fa-gauge-high',
    table: 'fas fa-table',
    patch: 'fas fa-code-branch',
    action: 'fas fa-bolt',
    error: 'fas fa-circle-exclamation',
  }
  return map[props.artifact.kind] || 'fas fa-cube'
})

/** 动作来源与执行：与 UiFrame 共用同一套逻辑 */
const artifactRef = computed(() => props.artifact)
const { actionState, resultMessage, actions, execute } = useArtifactActions(artifactRef, (action) => {
  if (action.kind === 'apply_patch') applied.value = true
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
    case 'query_data':
      return 'fas fa-database'
    case 'open_panel':
      return 'fas fa-table-columns'
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

/** 动作执行成功且是补丁应用 → 标记已应用 */
</script>

<style scoped>
.artifact-card {
  border: 1px solid var(--border, #2a3449);
  border-radius: 10px;
  background: var(--panel-bg, #1a2236);
  overflow: hidden;
}

.artifact-head {
  display: flex;
  align-items: center;
  gap: 9px;
  padding: 10px 13px;
  border-bottom: 1px solid rgba(74, 85, 104, 0.28);
}

.artifact-icon {
  color: var(--primary, #2962ff);
  display: inline-flex;
  font-size: 12px;
}

.artifact-title {
  font-size: 13px;
  font-weight: 500;
  color: var(--text, #e0e0e0);
}

.artifact-subtitle {
  font-size: 11.5px;
  color: #6f7b8a;
}

.spacer {
  flex: 1;
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

.enlarge-btn {
  height: 22px;
  padding: 0 8px;
  font-size: 11px;
  border-radius: 5px;
  border: 1px solid rgba(41, 98, 255, 0.5);
  background: rgba(41, 98, 255, 0.1);
  color: #7ea6ff;
  cursor: pointer;
  display: inline-flex;
  align-items: center;
  gap: 5px;
  font-family: inherit;
  white-space: nowrap;
}

.enlarge-btn:hover {
  background: rgba(41, 98, 255, 0.2);
  color: #a9c3ff;
}

.artifact-foot {
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 9px 13px;
  border-top: 1px solid rgba(74, 85, 104, 0.28);
  background: rgba(255, 255, 255, 0.015);
}

.artifact-source {
  flex: 1;
  font-size: 11px;
  color: #6f7b8a;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.action-btn {
  height: 23px;
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
