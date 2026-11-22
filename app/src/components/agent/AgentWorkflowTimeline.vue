<template>
  <div class="workflow">
    <div v-if="canExport" class="wf-toolbar">
      <button
        class="wf-tool-btn"
        type="button"
        title="复制当前回合的完整痕迹（markdown）"
        @click="copyTrace"
      >
        <i class="fas fa-copy"></i> 复制记录
      </button>
      <button
        class="wf-tool-btn"
        type="button"
        title="下载完整痕迹 JSON（含会话历史的持久化思考，保存到下载文件夹）"
        @click="downloadTrace"
      >
        <i class="fas fa-download"></i> 下载 JSON
      </button>
      <span v-if="toolHint" class="wf-tool-hint">{{ toolHint }}</span>
    </div>

    <div ref="scrollRef" class="workflow-col">
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
import { useChatStore } from '@/stores/chatStore'
import {
  buildTraceJson,
  formatEventsMarkdown,
  traceFilename,
} from '@/lib/agent/traceExport'

const session = inject(AGENT_SESSION_KEY)!
const scrollRef = ref<HTMLElement | null>(null)
const chatStore = useChatStore()

const events = computed(() => session.events.value)
const loading = computed(() => session.loading.value)

/** 会话里是否有可导出的历史思考（回合结束后事件流会清空，但历史还在） */
const hasHistory = computed(() =>
  chatStore.messages.some((m) => (m.thoughts?.length ?? 0) > 0),
)
const canExport = computed(() => events.value.length > 0 || hasHistory.value)

/** 复制/下载后的短暂提示 */
const toolHint = ref('')
let hintTimer: ReturnType<typeof setTimeout> | undefined
function flashHint(text: string) {
  toolHint.value = text
  clearTimeout(hintTimer)
  hintTimer = setTimeout(() => (toolHint.value = ''), 2500)
}

/** 写剪贴板（clipboard API 失败时退回 execCommand 兜底） */
async function copyText(text: string): Promise<boolean> {
  try {
    if (navigator.clipboard?.writeText) {
      await navigator.clipboard.writeText(text)
      return true
    }
  } catch { /* 走兜底 */ }
  try {
    const ta = document.createElement('textarea')
    ta.value = text
    ta.style.position = 'fixed'
    ta.style.opacity = '0'
    document.body.appendChild(ta)
    ta.select()
    const ok = document.execCommand('copy')
    document.body.removeChild(ta)
    return ok
  } catch {
    return false
  }
}

/** 复制当前回合的完整痕迹（markdown，可贴进对话/工单） */
async function copyTrace() {
  const ok = await copyText(formatEventsMarkdown(events.value))
  flashHint(ok ? '已复制' : '复制失败，请重试')
}

/** 下载完整痕迹（JSON：当前回合事件 + 会话历史持久化思考） */
function downloadTrace() {
  const history = chatStore.messages.map((m) => ({
    id: m.id,
    role: m.role,
    content: m.content,
    timestamp: m.timestamp,
    thoughts: m.thoughts,
    tokenUsage: m.tokenUsage,
    error: m.error,
  }))

  const json = buildTraceJson(events.value, history)
  const blob = new Blob([json], { type: 'application/json' })
  const url = URL.createObjectURL(blob)
  const a = document.createElement('a')
  a.href = url
  a.download = traceFilename()
  document.body.appendChild(a)
  a.click()
  document.body.removeChild(a)
  URL.revokeObjectURL(url)
  flashHint(`已下载 ${a.download}`)
}

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
  gap: '能力缺口',
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
  display: flex;
  flex-direction: column;
  padding: 10px 14px 0;
}

/* 导出工具栏：固定在滚动区上方 */
.wf-toolbar {
  flex: none;
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 0 2px 8px;
}

.wf-tool-btn {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  background: var(--panel-bg, #1a2236);
  color: #a0aec0;
  border: 1px solid var(--border, #2a3449);
  border-radius: 6px;
  padding: 4px 10px;
  font-size: 12px;
  cursor: pointer;
  transition: color 0.15s, border-color 0.15s;
}

.wf-tool-btn:hover {
  color: #e0e0e0;
  border-color: #2962ff;
}

.wf-tool-hint {
  font-size: 11.5px;
  color: #00c853;
}

.workflow-col {
  flex: 1;
  min-height: 0;
  overflow-y: auto;
  max-width: 812px;
  width: 100%;
  margin: 0 auto;
  padding-bottom: 16px;
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

/* 能力缺口：不是错误，是「这件事当前做不到」的显式声明 */
.wf-item.ev-gap {
  border-left-color: #ffb300;
  border-left-style: dashed;
  background: rgba(255, 179, 0, 0.06);
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
