<template>
  <div ref="rootRef" class="model-select">
    <button
      type="button"
      class="model-btn"
      :class="{ 'is-empty': !store.activeReady }"
      :disabled="disabled"
      :data-tooltip="
        store.activeReady ? '切换模型基底' : '当前基底未配置，请先在设置中填写 API Key'
      "
      @click="toggle"
    >
      <i class="fas fa-microchip"></i>
      <span class="model-btn__label">{{ buttonLabel }}</span>
      <i class="fas fa-chevron-up chevron" :class="{ 'is-open': open }"></i>
    </button>

    <div v-if="open" class="model-menu">
      <div class="model-menu__head">模型基底</div>
      <button
        v-for="base in store.readyBases"
        :key="base.presetId"
        type="button"
        class="model-item"
        :class="{ 'is-active': base.presetId === store.activeId }"
        @click="select(base.presetId)"
      >
        <span class="model-item__name">{{ presetLabel(base.presetId) }}</span>
        <span class="model-item__model">{{ base.model }}</span>
        <i v-if="base.presetId === store.activeId" class="fas fa-check"></i>
      </button>
      <div v-if="!store.readyBases.length" class="model-menu__empty">
        还没有配置好的模型基底
      </div>
      <div class="model-menu__foot">在「设置 → Agent 大语言模型配置」中管理</div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { computed, onMounted, onUnmounted, ref } from 'vue'
import { getModelBasePreset, type ModelBaseId } from '@/lib/agent/modelBases'
import { useAgentModelStore } from '@/stores/agentModel'

defineProps<{ disabled?: boolean }>()

const store = useAgentModelStore()
const rootRef = ref<HTMLElement | null>(null)
const open = ref(false)

const buttonLabel = computed(() => {
  const base = store.activeBase
  if (!base || !store.activeReady) return '未配置模型'
  return `${presetLabel(base.presetId)} · ${base.model}`
})

function presetLabel(id: ModelBaseId): string {
  return getModelBasePreset(id).label
}

function toggle() {
  open.value = !open.value
}

function select(id: ModelBaseId) {
  store.setActive(id)
  open.value = false
}

function onDocClick(e: MouseEvent) {
  const el = rootRef.value
  if (!el) return
  if (el.contains(e.target as Node)) return
  open.value = false
}

function onKeydown(e: KeyboardEvent) {
  if (e.key === 'Escape') open.value = false
}

onMounted(() => {
  setTimeout(() => document.addEventListener('click', onDocClick), 0)
  window.addEventListener('keydown', onKeydown)
})

onUnmounted(() => {
  document.removeEventListener('click', onDocClick)
  window.removeEventListener('keydown', onKeydown)
})
</script>

<style scoped>
.model-select {
  position: relative;
  flex: 0 0 auto;
}

.model-btn {
  height: 29px;
  max-width: 190px;
  padding: 0 9px;
  border-radius: 7px;
  border: 1px solid var(--border, #2a3449);
  background: transparent;
  color: #a0aec0;
  font-size: 11px;
  font-family: inherit;
  cursor: pointer;
  display: inline-flex;
  align-items: center;
  gap: 6px;
  white-space: nowrap;
}

.model-btn:hover:not(:disabled) {
  color: #e0e0e0;
  border-color: var(--primary, #2962ff);
}

.model-btn:disabled {
  opacity: 0.55;
  cursor: default;
}

.model-btn.is-empty {
  color: #ff9800;
  border-color: rgba(255, 152, 0, 0.35);
}

.model-btn i:first-child {
  font-size: 10px;
  opacity: 0.85;
}

.model-btn__label {
  max-width: 150px;
  overflow: hidden;
  text-overflow: ellipsis;
}

.chevron {
  font-size: 8px;
  opacity: 0.7;
  transition: transform 0.15s;
}

.chevron.is-open {
  transform: rotate(180deg);
}

.model-menu {
  position: absolute;
  bottom: calc(100% + 7px);
  right: 0;
  z-index: 60;
  width: 250px;
  padding: 7px;
  border: 1px solid var(--border, #2a3449);
  border-radius: 9px;
  background: var(--panel-bg, #1a2236);
  box-shadow: 0 14px 38px rgba(0, 0, 0, 0.55);
}

.model-menu__head {
  padding: 3px 8px 6px;
  font-size: 10.5px;
  color: #6f7b8a;
}

.model-item {
  display: flex;
  align-items: center;
  gap: 8px;
  width: 100%;
  padding: 7px 8px;
  border: none;
  border-radius: 6px;
  background: transparent;
  color: #a0aec0;
  font-family: inherit;
  font-size: 12px;
  text-align: left;
  cursor: pointer;
}

.model-item:hover {
  background: rgba(255, 255, 255, 0.05);
  color: #e0e0e0;
}

.model-item.is-active {
  background: rgba(41, 98, 255, 0.14);
  color: #7ea6ff;
}

.model-item__name {
  flex: 0 0 auto;
}

.model-item__model {
  flex: 1;
  min-width: 0;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
  font-size: 10.5px;
  color: #6f7b8a;
}

.model-item.is-active .model-item__model {
  color: #7ea6ff;
}

.model-menu__empty {
  padding: 10px 8px;
  font-size: 11.5px;
  color: #6f7b8a;
}

.model-menu__foot {
  margin-top: 4px;
  padding: 7px 8px 2px;
  border-top: 1px solid rgba(74, 85, 104, 0.3);
  font-size: 10px;
  color: #6f7b8a;
}
</style>
