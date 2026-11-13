<template>
  <Teleport to="body">
    <transition name="launcher">
      <button
        v-if="chatStore.visible"
        type="button"
        class="agent-launcher"
        data-tooltip="打开 Agent 分析面板"
        @click="openPanel"
      >
        <span class="dot"></span>
        <i class="fas fa-robot"></i>
        Agent
      </button>
    </transition>
  </Teleport>
</template>

<script setup lang="ts">
/**
 * 浮窗已降级为唤起入口。
 *
 * 对话、产物、分支、Agent 工作流全部并入「可视化分析 → Agent 分析面板」。
 * 这里只保留一个悬浮按钮，点击后切换到该面板。
 */
import { useChatStore } from '@/stores/chatStore'

const chatStore = useChatStore()

function openPanel() {
  window.dispatchEvent(new CustomEvent('open-agent-panel'))
  chatStore.visible = false
}
</script>

<style scoped>
.agent-launcher {
  position: fixed;
  right: 20px;
  bottom: 80px;
  z-index: 9999;
  display: inline-flex;
  align-items: center;
  gap: 7px;
  height: 36px;
  padding: 0 15px;
  border-radius: 20px;
  border: none;
  cursor: pointer;
  background: #2962ff;
  color: #fff;
  font-size: 12.5px;
  font-family: inherit;
  box-shadow: 0 6px 18px rgba(41, 98, 255, 0.38);
  transition: background 0.15s, transform 0.15s;
}

.agent-launcher:hover {
  background: #1f52d8;
  transform: translateY(-1px);
}

.agent-launcher .dot {
  width: 6px;
  height: 6px;
  border-radius: 50%;
  background: #7dffb0;
}

.launcher-enter-active,
.launcher-leave-active {
  transition: opacity 0.18s, transform 0.18s;
}

.launcher-enter-from,
.launcher-leave-to {
  opacity: 0;
  transform: translateY(6px);
}
</style>
