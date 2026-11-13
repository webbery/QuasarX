<template>
  <div class="artifact-card">
    <div class="artifact-head">
      <span class="artifact-icon"><i :class="iconClass"></i></span>
      <span class="artifact-title">{{ artifact.title }}</span>
      <span v-if="artifact.subtitle" class="artifact-subtitle">{{ artifact.subtitle }}</span>
      <span class="spacer"></span>
      <button
        v-if="renderer"
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
import { computed, reactive, ref } from 'vue'
import { getArtifactSpec } from '@/lib/agent/artifactRegistry'
import { runAction, type ActionContext } from '@/lib/agent/actionRegistry'
import type { ArtifactAction, ArtifactEnvelope } from '@/lib/agent/artifacts'
import { useChatStore } from '@/stores/chatStore'
import AgentFallbackArtifact from './AgentFallbackArtifact.vue'

const props = defineProps<{ artifact: ArtifactEnvelope }>()

const chatStore = useChatStore()
const collapsed = ref(false)

const actionState = reactive<Record<string, 'idle' | 'pending' | 'done'>>({})
const resultMessage = ref('')
const applied = ref(false)

const spec = computed(() => getArtifactSpec(props.artifact.kind))
const renderer = computed(() => spec.value?.render ?? null)

/** 只传渲染器真正需要的 props，避免多余属性落到 DOM 上 */
const rendererProps = computed<Record<string, unknown>>(() => {
  if (!spec.value) return {}
  const out: Record<string, unknown> = { payload: props.artifact.payload }
  const height = props.artifact.ui?.height
  if (height) out.height = height
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

/** 动作来源：顶层 actions 优先，其次 action 类型的 payload.actions */
const actions = computed<ArtifactAction[]>(() => {
  if (props.artifact.actions?.length) return props.artifact.actions
  const payload = props.artifact.payload as { actions?: ArtifactAction[] } | undefined
  if (props.artifact.kind === 'action' && payload?.actions?.length) return payload.actions
  return []
})

function actionIcon(action: ArtifactAction): string {
  const state = actionState[action.id]
  if (state === 'pending') return 'fas fa-spinner fa-spin'
  if (state === 'done') return 'fas fa-check'
  switch (action.kind) {
    case 'apply_patch':
      return 'fas fa-check'
    case 'run_backtest':
      return 'fas fa-play'
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

async function execute(action: ArtifactAction) {
  if (action.destructive && action.confirm) {
    const ok = window.confirm(
      `${action.confirm.title}${action.confirm.body ? `\n\n${action.confirm.body}` : ''}`,
    )
    if (!ok) return
  }

  actionState[action.id] = 'pending'
  resultMessage.value = ''

  const ctx: ActionContext = {
    artifact: props.artifact,
    sessionId: chatStore.currentSession?.id ?? 'default',
  }

  const result = await runAction(action, ctx)

  actionState[action.id] = result.ok ? 'done' : 'idle'
  resultMessage.value = result.message
    ? `${result.ok ? '✓' : '✕'} ${result.message}`
    : ''

  if (result.ok && action.kind === 'apply_patch') {
    applied.value = true
  }
}
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
