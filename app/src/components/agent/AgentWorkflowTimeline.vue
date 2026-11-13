<template>
  <div ref="scrollRef" class="workflow">
    <div class="workflow-col">
      <div v-if="events.length === 0" class="empty">
        <i class="fas fa-diagram-project"></i>
        <p>Agent 工作流将在此展示多 Agent 协作过程</p>
        <p class="hint">发送一条消息开始对话</p>
      </div>

      <div
        v-for="(evt, i) in events"
        :key="i"
        class="wf-item"
        :class="`ev-${evt.eventType}`"
      >
        <div class="wf-main">
          <div class="wf-head">
            <span class="agent-badge" :class="`is-${evt.agent}`">{{ agentLabel(evt.agent) }}</span>
            <span class="evt-label">{{ eventLabel(evt.eventType) }}</span>
            <span class="spacer"></span>
            <span class="wf-time">{{ formatTime(evt.timestamp) }}</span>
          </div>
          <div class="wf-body">{{ evt.content }}</div>
        </div>
      </div>

      <div v-if="loading" class="wf-item ev-response">
        <div class="wf-main">
          <div class="wf-head">
            <span class="agent-badge is-supervisor"><i class="fas fa-spinner fa-spin"></i> 处理中</span>
          </div>
        </div>
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { computed, inject, nextTick, ref, watch } from 'vue'
import type { AgentType } from '@/lib/agent/types'
import { AGENT_SESSION_KEY } from './sessionContext'

const session = inject(AGENT_SESSION_KEY)!
const scrollRef = ref<HTMLElement | null>(null)

const events = computed(() => session.events.value)
const loading = computed(() => session.loading.value)

const AGENT_LABELS: Record<string, string> = {
  supervisor: 'Supervisor',
  chat: 'Chat',
  strategy: 'Strategy',
  risk: 'Risk',
  portfolio: 'Portfolio',
  analysis: 'Analysis',
}

const EVENT_LABELS: Record<string, string> = {
  thought: '思考',
  tool_call: '调用工具',
  tool_result: '工具返回',
  artifact: '生成产物',
  response: '回复',
  error: '错误',
}

function agentLabel(agent: AgentType | string): string {
  return AGENT_LABELS[agent] || String(agent)
}

function eventLabel(type: string): string {
  return EVENT_LABELS[type] || type
}

function formatTime(ts: number): string {
  const d = new Date(ts)
  return `${String(d.getHours()).padStart(2, '0')}:${String(d.getMinutes()).padStart(2, '0')}:${String(d.getSeconds()).padStart(2, '0')}`
}

watch(
  () => events.value.length,
  async () => {
    await nextTick()
    const el = scrollRef.value
    if (el) el.scrollTop = el.scrollHeight
  },
)
</script>

<style scoped>
.workflow {
  flex: 1;
  min-height: 0;
  overflow-y: auto;
  padding: 16px 14px;
}

.workflow-col {
  max-width: 812px;
  margin: 0 auto;
  display: flex;
  flex-direction: column;
  gap: 7px;
}

.empty {
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  padding: 60px 20px;
  color: #6f7b8a;
  text-align: center;
}

.empty i {
  font-size: 42px;
  opacity: 0.3;
  margin-bottom: 12px;
}

.empty p {
  font-size: 13px;
  margin: 4px 0;
}

.empty .hint {
  font-size: 12px;
  color: #4b5563;
}

.wf-item {
  border-left: 2px solid var(--border, #2a3449);
  background: var(--panel-bg, #1a2236);
  border-radius: 0 9px 9px 0;
  padding: 10px 13px 10px 12px;
}

.wf-item.ev-thought {
  border-left-color: #6f7b8a;
}

.wf-item.ev-tool_call {
  border-left-color: #ff9800;
}

.wf-item.ev-tool_result {
  border-left-color: #00c853;
}

.wf-item.ev-artifact {
  border-left-color: #9a67d1;
}

.wf-item.ev-response {
  border-left-color: #2962ff;
}

.wf-item.ev-error {
  border-left-color: #ef5350;
  background: rgba(239, 35, 42, 0.05);
}

.wf-head {
  display: flex;
  align-items: center;
  gap: 9px;
  margin-bottom: 4px;
}

.spacer {
  flex: 1;
}

.agent-badge {
  font-size: 10.5px;
  padding: 1px 7px;
  border-radius: 20px;
  font-weight: 500;
  background: rgba(41, 98, 255, 0.14);
  color: #2962ff;
}

.agent-badge.is-supervisor {
  background: rgba(154, 103, 209, 0.16);
  color: #b18ae0;
}

.agent-badge.is-chat {
  background: rgba(41, 98, 255, 0.14);
  color: #2962ff;
}

.agent-badge.is-strategy {
  background: rgba(0, 200, 83, 0.14);
  color: #00c853;
}

.agent-badge.is-risk {
  background: rgba(239, 35, 42, 0.14);
  color: #ef5350;
}

.agent-badge.is-portfolio {
  background: rgba(255, 152, 0, 0.14);
  color: #ff9800;
}

.agent-badge.is-analysis {
  background: rgba(255, 152, 0, 0.14);
  color: #ff9800;
}

.evt-label {
  font-size: 11.5px;
  color: #e0e0e0;
}

.wf-time {
  font-size: 11px;
  color: #6f7b8a;
}

.wf-body {
  font-size: 12px;
  line-height: 1.65;
  color: #a0aec0;
  white-space: pre-wrap;
  word-break: break-word;
  max-height: 160px;
  overflow-y: auto;
}
</style>
