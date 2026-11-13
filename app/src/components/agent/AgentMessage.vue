<template>
  <div class="agent-msg" :class="[`is-${message.role}`, { 'is-editing': editing }]">
    <div class="msg-avatar">
      <i :class="message.role === 'user' ? 'fas fa-user' : 'fas fa-robot'"></i>
    </div>

    <div class="msg-body">
      <!-- 思考过程 -->
      <details v-if="message.thoughts?.length" class="msg-thoughts">
        <summary>
          <i class="fas fa-brain"></i>
          思考过程 · {{ message.thoughts.length }} 步
          <i class="fas fa-chevron-down chevron"></i>
        </summary>
        <div class="thoughts-body">
          <div v-for="(step, i) in message.thoughts" :key="step.id" class="thought-step">
            <span class="thought-index">第{{ i + 1 }}步</span>
            <span class="thought-text">{{ step.content }}</span>
          </div>
        </div>
      </details>

      <!-- 编辑态 -->
      <div v-if="editing" class="edit-box">
        <textarea v-model="draft" rows="3" class="edit-input" @keydown.meta.enter="submitEdit" @keydown.ctrl.enter="submitEdit"></textarea>
        <div class="edit-actions">
          <span class="edit-hint">编辑后发送会创建一个新分支，原对话保留</span>
          <button class="ghost-btn" @click="cancelEdit">取消</button>
          <button class="primary-btn" :disabled="!draft.trim()" @click="submitEdit">发送并分支</button>
        </div>
      </div>

      <!-- 正文 -->
      <template v-else>
        <div v-if="message.content" class="msg-bubble">
          <div
            v-if="message.role === 'assistant'"
            class="markdown-body"
            v-html="rendered"
          ></div>
          <div v-else class="plain-text">{{ message.content }}</div>
        </div>

        <!-- 产物 -->
        <div v-if="artifacts.length" class="msg-artifacts">
          <AgentArtifactCard
            v-for="artifact in artifacts"
            :key="artifact.id"
            :artifact="artifact"
          />
        </div>

        <!-- 加载中 -->
        <div v-if="pending" class="msg-bubble typing">
          <span></span><span></span><span></span>
        </div>

        <!-- 错误 -->
        <div v-if="message.error" class="msg-error">
          <i class="fas fa-circle-exclamation"></i>{{ message.error }}
        </div>
      </template>

      <!-- 分支导航 -->
      <div v-if="branch" class="branch-bar">
        <button class="branch-btn" data-tooltip="上一个分支" @click="$emit('switch-branch', message.id, -1)">
          <i class="fas fa-chevron-left"></i>
        </button>
        <span class="branch-label">分支 {{ branch.index + 1 }} / {{ branch.total }}</span>
        <button class="branch-btn" data-tooltip="下一个分支" @click="$emit('switch-branch', message.id, 1)">
          <i class="fas fa-chevron-right"></i>
        </button>
      </div>

      <!-- 悬浮操作 -->
      <div class="msg-actions">
        <template v-if="message.role === 'user'">
          <button class="mini-btn" data-tooltip="编辑并从此处分支" @click="startEdit">
            <i class="fas fa-pen"></i>
          </button>
          <button class="mini-btn" data-tooltip="从此处继续" @click="$emit('fork', message.id)">
            <i class="fas fa-code-branch"></i>
          </button>
        </template>
        <template v-else>
          <button class="mini-btn" data-tooltip="重新生成（产生兄弟分支）" :disabled="disabled" @click="$emit('regenerate', message.id)">
            <i class="fas fa-rotate-right"></i>
          </button>
          <button class="mini-btn" data-tooltip="从这里继续" @click="$emit('fork', message.id)">
            <i class="fas fa-code-branch"></i>
          </button>
        </template>
        <span v-if="message.tokenUsage" class="token-usage" :title="`Prompt ${message.tokenUsage.promptTokens} / Completion ${message.tokenUsage.completionTokens}`">
          {{ message.tokenUsage.totalTokens }} tok
        </span>
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { computed, ref } from 'vue'
import { renderMarkdown } from '@/lib/markdown'
import { useChatStore, type ChatMessage } from '@/stores/chatStore'
import AgentArtifactCard from './AgentArtifactCard.vue'

const props = defineProps<{
  message: ChatMessage
  branch?: { index: number; total: number } | null
  pending?: boolean
  disabled?: boolean
}>()

const emit = defineEmits<{
  (e: 'edit', messageId: string, text: string): void
  (e: 'regenerate', messageId: string): void
  (e: 'fork', messageId: string): void
  (e: 'switch-branch', messageId: string, delta: number): void
}>()

const chatStore = useChatStore()

const editing = ref(false)
const draft = ref('')

const rendered = computed(() => renderMarkdown(props.message.content))

const artifacts = computed(() => {
  const ids = props.message.artifactIds ?? []
  return ids
    .map((id) => chatStore.artifacts[id])
    .filter((a) => !!a)
})

function startEdit() {
  draft.value = props.message.content
  editing.value = true
}

function cancelEdit() {
  editing.value = false
  draft.value = ''
}

function submitEdit() {
  const text = draft.value.trim()
  if (!text) return
  editing.value = false
  emit('edit', props.message.id, text)
}
</script>

<style scoped>
.agent-msg {
  display: flex;
  gap: 10px;
  position: relative;
}

.agent-msg.is-user {
  flex-direction: row-reverse;
}

.msg-avatar {
  flex: 0 0 26px;
  width: 26px;
  height: 26px;
  border-radius: 7px;
  display: flex;
  align-items: center;
  justify-content: center;
  background: #202a42;
  color: #a0aec0;
  font-size: 11px;
}

.agent-msg:not(.is-user) .msg-avatar {
  background: rgba(41, 98, 255, 0.14);
  color: #2962ff;
}

.msg-body {
  flex: 1;
  min-width: 0;
  position: relative;
  display: flex;
  flex-direction: column;
  gap: 8px;
}

.agent-msg.is-user .msg-body {
  align-items: flex-end;
}

.msg-bubble {
  max-width: 100%;
  font-size: 13px;
  line-height: 1.68;
  background: var(--panel-bg, #1a2236);
  border: 1px solid rgba(74, 85, 104, 0.28);
  border-radius: 9px;
  padding: 9px 12px;
  color: var(--text, #e0e0e0);
}

.agent-msg.is-user .msg-bubble {
  background: rgba(41, 98, 255, 0.14);
  border-color: rgba(41, 98, 255, 0.35);
}

.plain-text {
  white-space: pre-wrap;
  word-break: break-word;
}

.markdown-body :deep(p) {
  margin: 0 0 8px;
}

.markdown-body :deep(p:last-child) {
  margin-bottom: 0;
}

.markdown-body :deep(code) {
  background: rgba(0, 0, 0, 0.3);
  padding: 1px 5px;
  border-radius: 4px;
  font-size: 12px;
}

.markdown-body :deep(pre) {
  background: rgba(0, 0, 0, 0.4);
  padding: 12px;
  border-radius: 8px;
  overflow-x: auto;
  margin: 8px 0;
}

.markdown-body :deep(pre code) {
  background: none;
  padding: 0;
}

.markdown-body :deep(ul),
.markdown-body :deep(ol) {
  margin: 6px 0;
  padding-left: 20px;
}

.markdown-body :deep(a) {
  color: #60a5fa;
}

.markdown-body :deep(table) {
  width: 100%;
  border-collapse: collapse;
  margin: 8px 0;
  font-size: 12px;
}

.markdown-body :deep(th),
.markdown-body :deep(td) {
  border: 1px solid rgba(255, 255, 255, 0.15);
  padding: 4px 8px;
  text-align: left;
}

.msg-artifacts {
  display: flex;
  flex-direction: column;
  gap: 10px;
  width: 100%;
}

/* 思考 */
.msg-thoughts {
  border: 1px solid rgba(74, 85, 104, 0.28);
  border-radius: 9px;
  background: rgba(255, 255, 255, 0.02);
  overflow: hidden;
}

.msg-thoughts summary {
  list-style: none;
  cursor: pointer;
  padding: 6px 11px;
  font-size: 11.5px;
  color: #6f7b8a;
  display: flex;
  align-items: center;
  gap: 7px;
}

.msg-thoughts summary::-webkit-details-marker {
  display: none;
}

.msg-thoughts summary:hover {
  color: #a0aec0;
}

.msg-thoughts .chevron {
  margin-left: auto;
  font-size: 9px;
}

.thoughts-body {
  padding: 8px 12px 10px;
  border-top: 1px solid rgba(74, 85, 104, 0.28);
  display: flex;
  flex-direction: column;
  gap: 6px;
}

.thought-step {
  display: flex;
  gap: 8px;
  font-size: 12px;
  line-height: 1.6;
  color: #6f7b8a;
}

.thought-index {
  flex: 0 0 auto;
  color: #2962ff;
  font-size: 11px;
}

.thought-text {
  white-space: pre-wrap;
}

/* 编辑 */
.edit-box {
  width: 100%;
  border: 1px solid #2962ff;
  border-radius: 9px;
  background: var(--panel-bg, #1a2236);
  padding: 8px;
}

.edit-input {
  width: 100%;
  border: none;
  outline: none;
  resize: vertical;
  background: transparent;
  color: #e0e0e0;
  font-size: 13px;
  line-height: 1.6;
  font-family: inherit;
}

.edit-actions {
  display: flex;
  align-items: center;
  gap: 8px;
  margin-top: 6px;
}

.edit-hint {
  flex: 1;
  font-size: 11px;
  color: #6f7b8a;
}

.ghost-btn,
.primary-btn {
  height: 24px;
  padding: 0 10px;
  font-size: 11.5px;
  border-radius: 5px;
  cursor: pointer;
  font-family: inherit;
}

.ghost-btn {
  background: transparent;
  border: 1px solid var(--border, #2a3449);
  color: #a0aec0;
}

.primary-btn {
  background: #2962ff;
  border: 1px solid #2962ff;
  color: #fff;
}

.primary-btn:disabled {
  opacity: 0.5;
  cursor: default;
}

/* 分支 */
.branch-bar {
  display: flex;
  align-items: center;
  gap: 6px;
  font-size: 11px;
  color: #6f7b8a;
}

.branch-btn {
  width: 20px;
  height: 20px;
  border-radius: 5px;
  border: 1px solid var(--border, #2a3449);
  background: transparent;
  color: #a0aec0;
  cursor: pointer;
  font-size: 9px;
}

.branch-btn:hover {
  border-color: #2962ff;
  color: #e0e0e0;
}

.branch-label {
  font-variant-numeric: tabular-nums;
}

/* 悬浮操作 */
.msg-actions {
  position: absolute;
  top: -10px;
  right: 0;
  display: flex;
  align-items: center;
  gap: 4px;
  padding: 2px 4px;
  background: var(--panel-bg, #1a2236);
  border: 1px solid var(--border, #2a3449);
  border-radius: 8px;
  opacity: 0;
  transition: opacity 0.12s;
  z-index: 4;
}

.agent-msg:hover .msg-actions {
  opacity: 1;
}

.mini-btn {
  width: 20px;
  height: 20px;
  border-radius: 5px;
  border: none;
  background: transparent;
  color: #6f7b8a;
  cursor: pointer;
  font-size: 10px;
}

.mini-btn:hover:not(:disabled) {
  color: #e0e0e0;
  background: rgba(255, 255, 255, 0.06);
}

.mini-btn:disabled {
  opacity: 0.4;
  cursor: default;
}

.token-usage {
  font-size: 10px;
  color: #6f7b8a;
  padding: 0 4px;
}

/* 加载 / 错误 */
.typing {
  display: flex;
  gap: 4px;
  align-items: center;
  width: fit-content;
  padding: 12px 14px;
}

.typing span {
  width: 6px;
  height: 6px;
  border-radius: 50%;
  background: #2962ff;
  animation: typing 1.4s infinite;
}

.typing span:nth-child(2) {
  animation-delay: 0.2s;
}

.typing span:nth-child(3) {
  animation-delay: 0.4s;
}

@keyframes typing {
  0%,
  60%,
  100% {
    transform: translateY(0);
    opacity: 0.5;
  }
  30% {
    transform: translateY(-6px);
    opacity: 1;
  }
}

.msg-error {
  display: flex;
  gap: 8px;
  align-items: center;
  font-size: 12px;
  color: #ef5350;
}

/* 用户消息的操作按钮在左下方更自然 */
.agent-msg.is-user .msg-actions {
  right: auto;
  left: 0;
}
</style>
