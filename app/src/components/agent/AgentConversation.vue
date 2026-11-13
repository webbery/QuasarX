<template>
  <div ref="scrollRef" class="conversation">
    <div class="conversation-col">
      <!-- 空态 -->
      <div v-if="messages.length === 0 && !session.loading.value" class="empty-state">
        <div class="empty-icon"><i class="fas fa-robot"></i></div>
        <p class="empty-title">Agent 分析面板</p>
        <p class="empty-hint">直接提问即可。Agent 会把图表、表格、策略修改提案作为「产物」直接画在对话里。</p>
        <div class="empty-examples">
          <button
            v-for="example in EXAMPLES"
            :key="example"
            class="example-chip"
            @click="$emit('quick-send', example)"
          >
            {{ example }}
          </button>
        </div>
      </div>

      <!-- 消息流 -->
      <AgentMessage
        v-for="msg in messages"
        :key="msg.id"
        :message="msg"
        :branch="chatStore.branchInfo(msg.id)"
        :pending="isPending(msg)"
        :disabled="session.loading.value"
        @edit="onEdit"
        @regenerate="onRegenerate"
        @fork="onFork"
        @switch-branch="onSwitchBranch"
      />
    </div>
  </div>
</template>

<script setup lang="ts">
import { computed, inject, nextTick, ref, watch } from 'vue'
import { useChatStore, type ChatMessage } from '@/stores/chatStore'
import { AGENT_SESSION_KEY } from './sessionContext'
import AgentMessage from './AgentMessage.vue'

const emit = defineEmits<{
  (e: 'fork', messageId: string): void
  (e: 'quick-send', text: string): void
}>()

const chatStore = useChatStore()
const session = inject(AGENT_SESSION_KEY)!

const scrollRef = ref<HTMLElement | null>(null)

const messages = computed<ChatMessage[]>(() => chatStore.messages)

const EXAMPLES = [
  '把最近一次回测的累计收益和回撤画出来',
  '哪些标的拖累了收益？给我一张归因表',
  '把止损线从 8% 调到 6%',
]

/** 最后一条 assistant 且内容为空 → 显示打字指示 */
function isPending(msg: ChatMessage): boolean {
  if (!session.loading.value) return false
  const last = messages.value[messages.value.length - 1]
  return !!last && last.id === msg.id && msg.role === 'assistant' && !msg.content
}

function onEdit(messageId: string, text: string) {
  session.editAndSend(messageId, text)
}

function onRegenerate(messageId: string) {
  session.regenerate(messageId)
}

function onFork(messageId: string) {
  emit('fork', messageId)
}

function onSwitchBranch(messageId: string, delta: number) {
  chatStore.switchBranch(messageId, delta)
}

/** 新消息或加载状态变化时滚到底部 */
watch(
  () => [messages.value.length, session.loading.value, chatStore.currentLeafId] as const,
  async () => {
    await nextTick()
    const el = scrollRef.value
    if (el) el.scrollTop = el.scrollHeight
  },
)

defineExpose({
  scrollToBottom: async () => {
    await nextTick()
    const el = scrollRef.value
    if (el) el.scrollTop = el.scrollHeight
  },
})
</script>

<style scoped>
.conversation {
  flex: 1;
  min-height: 0;
  overflow-y: auto;
  padding: 16px 14px 10px;
}

.conversation-col {
  max-width: 812px;
  margin: 0 auto;
  display: flex;
  flex-direction: column;
  gap: 16px;
}

.empty-state {
  display: flex;
  flex-direction: column;
  align-items: center;
  text-align: center;
  padding: 56px 20px 20px;
  color: #6f7b8a;
}

.empty-icon {
  width: 46px;
  height: 46px;
  border-radius: 12px;
  display: flex;
  align-items: center;
  justify-content: center;
  background: rgba(41, 98, 255, 0.12);
  color: #2962ff;
  font-size: 20px;
  margin-bottom: 14px;
}

.empty-title {
  font-size: 14px;
  color: #e0e0e0;
  margin: 0 0 6px;
}

.empty-hint {
  font-size: 12.5px;
  margin: 0 0 18px;
  max-width: 420px;
  line-height: 1.65;
}

.empty-examples {
  display: flex;
  flex-direction: column;
  gap: 8px;
  width: 100%;
  max-width: 420px;
}

.example-chip {
  text-align: left;
  padding: 9px 12px;
  border-radius: 9px;
  border: 1px solid rgba(74, 85, 104, 0.4);
  background: transparent;
  color: #a0aec0;
  font-size: 12.5px;
  cursor: pointer;
  font-family: inherit;
  transition: all 0.15s;
}

.example-chip:hover {
  border-color: #2962ff;
  color: #e0e0e0;
  background: rgba(41, 98, 255, 0.06);
}
</style>
