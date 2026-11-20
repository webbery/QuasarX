<template>
  <div class="ui-progress" :class="{ 'is-compact': density === 'compact' }">
    <div class="ui-progress__head">
      <span class="ui-progress__icon" :class="`is-${status}`">
        <i :class="iconClass"></i>
      </span>
      <span class="ui-progress__label">{{ displayLabel }}</span>
      <span v-if="detail" class="ui-progress__detail">{{ detail }}</span>
    </div>

    <!-- 阶段条：有 steps 就按阶段点亮，没有就只在上面显示一句话 -->
    <div v-if="steps.length" class="ui-progress__steps">
      <span
        v-for="(s, i) in steps"
        :key="s"
        class="ui-progress__step"
        :class="{
          'is-done': i < currentStep || status === 'done',
          'is-current': i === currentStep && status === 'running',
          'is-failed': i === currentStep && status === 'failed',
        }"
      >{{ s }}</span>
    </div>

    <!-- 进度槽：阶段明确时按阶段推进，否则走不确定动画 -->
    <div class="ui-progress__track">
      <div
        class="ui-progress__fill"
        :class="[`is-${status}`, { 'is-indeterminate': indeterminate }]"
        :style="fillStyle"
      ></div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { computed } from 'vue'
import type { ArtifactDensity } from '@/lib/agent/artifacts'
import type { UiProgressData } from '@/lib/charts/types'

const props = withDefaults(
  defineProps<{
    data: Record<string, unknown>
    params?: Record<string, unknown>
    height?: number
    density?: ArtifactDensity
  }>(),
  { density: 'full' },
)

const progress = computed<UiProgressData>(() => (props.data ?? {}) as unknown as UiProgressData)

const status = computed(() => progress.value.status ?? 'running')
const detail = computed(() => progress.value.detail ?? '')
const steps = computed(() => progress.value.steps ?? [])

/** 当前阶段下标；done 时视为全部点亮 */
const currentStep = computed(() => {
  if (status.value === 'done') return steps.value.length
  const raw = progress.value.step
  return typeof raw === 'number' ? Math.min(Math.max(raw, 0), steps.value.length) : 0
})

const displayLabel = computed(() => {
  const label = progress.value.label?.trim()
  if (label) return label
  if (status.value === 'done') return '已完成'
  if (status.value === 'failed') return '执行失败'
  return '执行中…'
})

/** 只有阶段明确时才画确定进度；其余走不确定动画（不做百分比假精度） */
const indeterminate = computed(() => status.value === 'running' && steps.value.length === 0)

const fillStyle = computed(() => {
  if (indeterminate.value) return {}
  const total = steps.value.length || 1
  const ratio = status.value === 'done' ? 1 : currentStep.value / total
  return { width: `${Math.round(ratio * 100)}%` }
})

const iconClass = computed(() => {
  if (status.value === 'done') return 'fas fa-circle-check'
  if (status.value === 'failed') return 'fas fa-circle-exclamation'
  return 'fas fa-spinner fa-spin'
})
</script>

<style scoped>
.ui-progress {
  padding: 14px 16px 16px;
}

.ui-progress__head {
  display: flex;
  align-items: center;
  gap: 8px;
  font-size: 12.5px;
}

.ui-progress__icon {
  font-size: 12px;
  display: inline-flex;
}

.ui-progress__icon.is-running {
  color: var(--primary, #2962ff);
}

.ui-progress__icon.is-done {
  color: #26a69a;
}

.ui-progress__icon.is-failed {
  color: #ef5350;
}

.ui-progress__label {
  color: var(--text, #e0e0e0);
}

.ui-progress__detail {
  margin-left: auto;
  font-size: 11.5px;
  color: #6f7b8a;
}

.ui-progress__steps {
  display: flex;
  flex-wrap: wrap;
  gap: 6px;
  margin-top: 10px;
}

.ui-progress__step {
  font-size: 11px;
  padding: 2px 8px;
  border-radius: 10px;
  color: #6f7b8a;
  background: rgba(74, 85, 104, 0.18);
  white-space: nowrap;
}

.ui-progress__step.is-done {
  color: #26a69a;
  background: rgba(38, 166, 154, 0.14);
}

.ui-progress__step.is-current {
  color: #fff;
  background: var(--primary, #2962ff);
}

.ui-progress__step.is-failed {
  color: #fff;
  background: #ef5350;
}

.ui-progress__track {
  position: relative;
  height: 4px;
  margin-top: 12px;
  border-radius: 2px;
  background: rgba(74, 85, 104, 0.28);
  overflow: hidden;
}

.ui-progress__fill {
  height: 100%;
  border-radius: 2px;
  background: var(--primary, #2962ff);
  transition: width 0.35s ease;
}

.ui-progress__fill.is-done {
  background: #26a69a;
}

.ui-progress__fill.is-failed {
  background: #ef5350;
}

/* 不确定态：一条 30% 宽的滑块来回跑，比假百分比诚实 */
.ui-progress__fill.is-indeterminate {
  width: 30%;
  animation: ui-progress-slide 1.2s ease-in-out infinite;
}

@keyframes ui-progress-slide {
  0% {
    transform: translateX(-100%);
  }
  100% {
    transform: translateX(340%);
  }
}

.ui-progress.is-compact {
  padding: 8px 12px 10px;
}

.ui-progress.is-compact .ui-progress__steps {
  display: none;
}
</style>
