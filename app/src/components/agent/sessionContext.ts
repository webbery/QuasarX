/**
 * Agent 会话上下文
 *
 * AgentPanel 创建一次 useAgentSession，通过 provide 下发，
 * 避免每个子组件各自持有一份事件流。
 */

import type { InjectionKey } from 'vue'
import type { useAgentSession } from '@/composables/useAgentSession'

export type AgentSession = ReturnType<typeof useAgentSession>

export const AGENT_SESSION_KEY: InjectionKey<AgentSession> = Symbol('agentSession')
